// The five-hour session gauge, read from the horse's mouth.
//
// Claude Code's own status line gets this number from an OAuth endpoint the
// app uses for itself: GET /api/oauth/usage returns the current utilization of
// the five-hour and seven-day windows as whole percentages, plus the instant
// each one resets. That is the real figure the server is enforcing -- not a
// reconstruction from transcripts -- so there is nothing here to calibrate and
// no error bar to apologise for.
//
// The token is the same Claude Code login already stored on this machine: the
// macOS Keychain item "Claude Code-credentials", or ~/.claude/.credentials.json
// on platforms without a Keychain. It never leaves the host -- the device only
// ever sees the rendered percentage.
//
// Everything in here fails soft. No token, no network, an endpoint that has
// moved: the result is a null window, the card says so, and the rest of the
// dashboard carries on.

import { execFile } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import { homedir } from 'node:os';
import path from 'node:path';
import { promisify } from 'node:util';

const execFileAsync = promisify(execFile);

const USAGE_URL = 'https://api.anthropic.com/api/oauth/usage';
const CACHE_MS = 60_000;   // the window moves in whole percent; once a minute is plenty
const TIMEOUT_MS = 4_000;

const BACKOFF_MS = 5 * 60_000;      // minimum pause after a 429
const MAX_BACKOFF_MS = 30 * 60_000; // ignore absurd Retry-After values

let cache = { at: 0, data: null };
let retryAt = 0;
let lastError = null;

function parseToken(raw) {
  if (!raw) return null;
  try {
    const c = JSON.parse(raw);
    return c.claudeAiOauth?.accessToken ?? c.accessToken ?? null;
  } catch {
    return raw.startsWith('sk-') ? raw : null;
  }
}

async function tokenFromKeychain() {
  if (process.platform !== 'darwin') return null;
  try {
    const { stdout } = await execFileAsync('security', [
      'find-generic-password', '-s', 'Claude Code-credentials', '-w',
    ]);
    return parseToken(stdout.trim());
  } catch {
    return null;
  }
}

async function tokenFromFile() {
  try {
    return parseToken(await readFile(path.join(homedir(), '.claude', '.credentials.json'), 'utf8'));
  } catch {
    return null;
  }
}

async function getToken() {
  return (await tokenFromKeychain()) ?? (await tokenFromFile());
}

// Shape: { five_hour: {utilization, resets_at} | null, seven_day: ... | null }
export async function getUsageLimits(now = Date.now()) {
  if (cache.data && now - cache.at < CACHE_MS) return cache.data;

  const empty = { five_hour: null, seven_day: null, error: null };
  // On failure keep showing the last good reading (it moves in whole percent,
  // a few minutes stale beats an empty ring) and tag it with the error.
  const failed = (error) => (cache.data ? { ...cache.data, error } : { ...empty, error });

  // Backing off after a 429/failure: don't hit the endpoint again until then.
  if (now < retryAt) return failed(lastError);

  const token = await getToken();
  if (!token) {
    return { ...empty, error: 'no Claude Code login found' };
  }

  try {
    const res = await fetch(USAGE_URL, {
      headers: {
        accept: 'application/json',
        authorization: `Bearer ${token}`,
        'anthropic-beta': 'oauth-2025-04-20',
      },
      signal: AbortSignal.timeout(TIMEOUT_MS),
    });
    if (!res.ok) {
      const retryAfter = Number(res.headers.get('retry-after'));
      const wait = res.status === 429
        ? Math.min(Math.max(Number.isFinite(retryAfter) && retryAfter > 0 ? retryAfter * 1000 : BACKOFF_MS, BACKOFF_MS), MAX_BACKOFF_MS)
        : CACHE_MS;
      lastError = `usage endpoint ${res.status}`;
      retryAt = now + wait;
      return failed(lastError);
    }

    const body = await res.json();
    const data = {
      five_hour: body.five_hour ?? null,
      seven_day: body.seven_day ?? null,
      error: null,
    };
    cache = { at: now, data };
    retryAt = 0;
    return data;
  } catch (err) {
    lastError = err.name === 'TimeoutError' ? 'usage endpoint timeout' : String(err.message ?? err);
    retryAt = now + CACHE_MS;
    return failed(lastError);
  }
}
