// Local source: reads Claude Code's own transcripts under ~/.claude/projects.
//
// Every assistant turn is logged as a JSONL line carrying `message.usage`, so
// the transcripts are a complete per-request token log for this machine. Files
// are append-only, so we keep a byte offset per file and only parse what is new
// on each scan -- a full re-parse of a busy ~/.claude every few seconds would
// be wasteful.
//
// Two gotchas this handles:
//   1. The same request is often written more than once (retries, replays), so
//      events are de-duplicated on requestId.
//   2. Files are occasionally rewritten or compacted; if a file shrinks we
//      reset its offset and re-read from the start.

import { createReadStream } from 'node:fs';
import { readdir, stat } from 'node:fs/promises';
import { createInterface } from 'node:readline';
import { homedir } from 'node:os';
import path from 'node:path';
import { costUSD } from '../pricing.mjs';

const RETAIN_DAYS = 35;

export class LocalSource {
  constructor({ root = path.join(homedir(), '.claude', 'projects') } = {}) {
    this.root = root;
    this.offsets = new Map(); // file -> bytes already parsed
    this.seen = new Set(); // requestId de-dup
    this.events = [];
  }

  get label() {
    return 'local';
  }

  async refresh() {
    for (const file of await this.#transcripts()) {
      let size;
      try {
        size = (await stat(file)).size;
      } catch {
        continue; // deleted between listing and stat
      }
      let from = this.offsets.get(file) ?? 0;
      if (size < from) from = 0; // truncated or rewritten
      if (size === from) continue;
      await this.#ingest(file, from, size);
      this.offsets.set(file, size);
    }
    this.#prune();
    return this.events;
  }

  async #transcripts() {
    let dirs;
    try {
      dirs = await readdir(this.root, { withFileTypes: true });
    } catch {
      return [];
    }
    const out = [];
    for (const d of dirs) {
      if (!d.isDirectory()) continue;
      const dir = path.join(this.root, d.name);
      const files = await readdir(dir).catch(() => []);
      for (const f of files) if (f.endsWith('.jsonl')) out.push(path.join(dir, f));
    }
    return out;
  }

  async #ingest(file, start, end) {
    const stream = createReadStream(file, { start, end: end - 1, encoding: 'utf8' });
    const lines = createInterface({ input: stream, crlfDelay: Infinity });
    for await (const line of lines) {
      if (!line.startsWith('{')) continue;
      let row;
      try {
        row = JSON.parse(line);
      } catch {
        continue; // a partial trailing line; the next scan re-reads it
      }

      const usage = row?.message?.usage;
      if (!usage) continue;

      const id = row.requestId ?? row.message?.id ?? row.uuid;
      if (id) {
        if (this.seen.has(id)) continue;
        this.seen.add(id);
      }

      const model = row.message?.model ?? row.model ?? null;
      const c5 = usage.cache_creation?.ephemeral_5m_input_tokens ?? 0;
      const c1h = usage.cache_creation?.ephemeral_1h_input_tokens ?? 0;

      this.events.push({
        t: Date.parse(row.timestamp ?? '') || Date.now(),
        model,
        project: row.cwd ? path.basename(row.cwd) : 'unknown',
        session: row.sessionId ?? null,
        input: usage.input_tokens ?? 0,
        cacheRead: usage.cache_read_input_tokens ?? 0,
        cacheWrite: c5 + c1h,
        output: usage.output_tokens ?? 0,
        webSearch: usage.server_tool_use?.web_search_requests ?? 0,
        cost: costUSD(model, usage),
      });
    }
  }

  #prune() {
    const cutoff = Date.now() - RETAIN_DAYS * 86_400_000;
    if (this.events.some((e) => e.t < cutoff)) {
      this.events = this.events.filter((e) => e.t >= cutoff);
    }
    this.events.sort((a, b) => a.t - b.t);
  }
}
