// Admin API source: organization-wide usage and cost from the Anthropic
// Usage & Cost Admin API.
//
// These two endpoints are deliberately not in the Anthropic SDKs -- they are
// raw HTTP only -- so this uses fetch directly.
//   GET /v1/organizations/usage_report/messages   token counts, 1m/1h/1d buckets
//   GET /v1/organizations/cost_report             billed USD, 1d buckets only
//
// The usage report gives the granularity (hourly, per model) and the cost
// report gives the authoritative dollars. We compute an estimated cost from the
// token counts for the hourly sparkline, then override the today/month totals
// with the cost report where it has data.
//
// Requires an Admin API credential: an admin key (sk-ant-admin...) or an
// org:admin OAuth token. Regular API keys are rejected, and the Admin API is
// unavailable for individual (non-organization) accounts.

import { costUSD } from '../pricing.mjs';

const BASE = 'https://api.anthropic.com/v1/organizations';
const UA = 'claude-status-cube/1.0';
const TIMEOUT_MS = 15_000; // per page; a hung request must not stall the refresh loop

export class AdminSource {
  constructor({ adminKey, oauthToken, usageWindowHours = 168 } = {}) {
    if (!adminKey && !oauthToken) {
      throw new Error('admin source needs ANTHROPIC_ADMIN_KEY or ANTHROPIC_ADMIN_OAUTH_TOKEN');
    }
    this.adminKey = adminKey;
    this.oauthToken = oauthToken;
    this.usageWindowHours = Math.min(usageWindowHours, 168); // API max for 1h buckets
    this.events = [];
    this.billed = null; // { byDay: Map<'YYYY-MM-DD', usd>, total: usd }
  }

  get label() {
    return 'admin';
  }

  get hasRequestCounts() {
    return false; // the usage report reports tokens, not request counts
  }

  #headers() {
    const h = {
      'anthropic-version': '2023-06-01',
      'user-agent': UA,
    };
    if (this.oauthToken) h.authorization = `Bearer ${this.oauthToken}`;
    else h['x-api-key'] = this.adminKey;
    return h;
  }

  async #getAll(endpoint, params) {
    const pages = [];
    let page = null;
    for (let i = 0; i < 20; i++) {
      const qs = new URLSearchParams(params);
      if (page) qs.set('page', page);
      const res = await fetch(`${BASE}${endpoint}?${qs}`, {
        headers: this.#headers(),
        signal: AbortSignal.timeout(TIMEOUT_MS),
      });
      if (!res.ok) {
        const body = await res.text().catch(() => '');
        throw new Error(`${endpoint} -> ${res.status} ${res.statusText}: ${body.slice(0, 300)}`);
      }
      const json = await res.json();
      pages.push(...(json.data ?? []));
      if (!json.has_more || !json.next_page) break;
      page = json.next_page;
    }
    return pages;
  }

  async refresh() {
    const now = new Date();
    const usageStart = new Date(now.getTime() - this.usageWindowHours * 3_600_000);
    // The month is the local calendar's, like every other date on the cube.
    // Cost buckets are whole UTC days, so it starts at the billing day dated
    // the local 1st, and each day is keyed by its date (cards.mjs looks today
    // up by the local date).
    const monthStart = new Date(Date.UTC(now.getFullYear(), now.getMonth(), 1));

    const [usageBuckets, costBuckets] = await Promise.all([
      this.#getAll('/usage_report/messages', [
        ['starting_at', usageStart.toISOString()],
        ['ending_at', now.toISOString()],
        ['bucket_width', '1h'],
        ['limit', String(this.usageWindowHours)],
        ['group_by[]', 'model'],
        ['group_by[]', 'workspace_id'], // without it every row's workspace_id is null
        ['group_by[]', 'inference_geo'], // US-only rows cost 1.1x (pricing.mjs)
      ]),
      this.#getAll('/cost_report', [
        ['starting_at', monthStart.toISOString()],
        ['ending_at', now.toISOString()],
        ['bucket_width', '1d'],
        ['limit', '31'],
        ['group_by[]', 'description'],
      ]),
    ]);

    this.events = [];
    for (const bucket of usageBuckets) {
      const t = Date.parse(bucket.starting_at);
      for (const r of bucket.results ?? []) {
        const usage = {
          input_tokens: r.uncached_input_tokens ?? 0,
          cache_read_input_tokens: r.cache_read_input_tokens ?? 0,
          cache_creation: r.cache_creation ?? {},
          output_tokens: r.output_tokens ?? 0,
          inference_geo: r.inference_geo,
        };
        const cw =
          (r.cache_creation?.ephemeral_5m_input_tokens ?? 0) +
          (r.cache_creation?.ephemeral_1h_input_tokens ?? 0);
        this.events.push({
          t,
          model: r.model,
          project: r.workspace_id ?? 'default',
          session: null,
          input: usage.input_tokens,
          cacheRead: usage.cache_read_input_tokens,
          cacheWrite: cw,
          output: usage.output_tokens,
          webSearch: r.server_tool_use?.web_search_requests ?? 0,
          cost: costUSD(r.model, usage),
        });
      }
    }

    // `amount` is a decimal string in the lowest currency unit (cents).
    const byDay = new Map();
    let total = 0;
    for (const bucket of costBuckets) {
      const day = bucket.starting_at.slice(0, 10);
      let sum = 0;
      for (const r of bucket.results ?? []) sum += Number(r.amount ?? 0) / 100;
      byDay.set(day, (byDay.get(day) ?? 0) + sum);
      total += sum;
    }
    this.billed = { byDay, total };

    this.events.sort((a, b) => a.t - b.t);
    return this.events;
  }
}
