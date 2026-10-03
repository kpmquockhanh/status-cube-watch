// Local source: reads Claude Code's own transcripts under ~/.claude/projects.
//
// Every assistant turn is logged as a JSONL line carrying `message.usage`, so
// the transcripts are a complete per-request token log for this machine. Files
// are append-only, so we keep a byte offset per file and only parse what is new
// on each scan -- a full re-parse of a busy ~/.claude every few seconds would
// be wasteful.
//
// Gotchas this handles:
//   1. The same request is often written more than once (retries, replays), so
//      events are de-duplicated on requestId.
//   2. Files are occasionally rewritten or compacted; if a file shrinks we
//      reset its offset and re-read from the start.
//   3. A scan can land while Claude Code is mid-way through writing a line. The
//      offset only advances past the last complete line ('\n'), so the rest of
//      a half-written line is picked up whole on the next scan.
//   4. Subagent turns live deeper, in <project>/<session>/subagents/*.jsonl, so
//      the scan is recursive.

import { open, readdir, stat } from 'node:fs/promises';
import { homedir } from 'node:os';
import path from 'node:path';
import { costUSD } from '../pricing.mjs';

const RETAIN_DAYS = 35;
const CHUNK = 1 << 20; // read size; lines are reassembled across chunks

export class LocalSource {
  constructor({ root = path.join(homedir(), '.claude', 'projects') } = {}) {
    this.root = root;
    this.files = new Map(); // file -> { offset, size, mtimeMs }; offset = bytes parsed
    this.seen = new Map(); // requestId -> event time, for de-dup; pruned with events
    this.events = [];
  }

  get label() {
    return 'local';
  }

  async refresh() {
    const listed = new Set(await this.#transcripts());
    let added = 0;
    for (const file of listed) {
      let st;
      try {
        st = await stat(file);
      } catch {
        continue; // deleted between listing and stat
      }
      const prev = this.files.get(file);
      if (prev && prev.size === st.size && prev.mtimeMs === st.mtimeMs) continue; // unchanged
      let from = prev?.offset ?? 0;
      if (st.size < from) from = 0; // truncated or rewritten
      const { offset, count } = await this.#ingest(file, from, st.size);
      this.files.set(file, { offset, size: st.size, mtimeMs: st.mtimeMs });
      added += count;
    }
    for (const file of this.files.keys()) if (!listed.has(file)) this.files.delete(file);
    this.#prune(added > 0);
    return this.events;
  }

  async #transcripts() {
    let entries;
    try {
      entries = await readdir(this.root, { recursive: true });
    } catch {
      return [];
    }
    return entries.filter((f) => f.endsWith('.jsonl')).map((f) => path.join(this.root, f));
  }

  // Parse the complete lines in [start, end). Returns the offset just past the
  // last '\n' seen (so a trailing partial line is re-read next time) and how
  // many new events were added. Lines are split on raw bytes and decoded only
  // once whole, so a multi-byte character cut by the scan cannot be mangled.
  async #ingest(file, start, end) {
    let fh;
    try {
      fh = await open(file, 'r');
    } catch {
      return { offset: start, count: 0 };
    }
    let pos = start;
    let done = start;
    let carry = Buffer.alloc(0);
    let count = 0;
    try {
      while (pos < end) {
        const buf = Buffer.allocUnsafe(Math.min(CHUNK, end - pos));
        const { bytesRead } = await fh.read(buf, 0, buf.length, pos);
        if (bytesRead === 0) break;
        pos += bytesRead;
        const chunk = Buffer.concat([carry, buf.subarray(0, bytesRead)]);
        let lineStart = 0;
        for (let nl = chunk.indexOf(10); nl !== -1; nl = chunk.indexOf(10, lineStart)) {
          if (this.#line(chunk.toString('utf8', lineStart, nl))) count++;
          lineStart = nl + 1;
        }
        carry = chunk.subarray(lineStart);
        done = pos - carry.length;
      }
    } finally {
      await fh.close();
    }
    return { offset: done, count };
  }

  // One transcript line; true when it added an event.
  #line(line) {
    if (!line.startsWith('{')) return false;
    let row;
    try {
      row = JSON.parse(line);
    } catch {
      return false; // not JSON; complete lines are never retried
    }

    const usage = row?.message?.usage;
    if (!usage) return false;

    const t = Date.parse(row.timestamp ?? '') || Date.now();
    const id = row.requestId ?? row.message?.id ?? row.uuid;
    if (id) {
      if (this.seen.has(id)) return false;
      this.seen.set(id, t);
    }

    const model = row.message?.model ?? row.model ?? null;
    const c5 = usage.cache_creation?.ephemeral_5m_input_tokens ?? 0;
    const c1h = usage.cache_creation?.ephemeral_1h_input_tokens ?? 0;
    const cacheWrite = usage.cache_creation ? c5 + c1h : usage.cache_creation_input_tokens ?? 0;

    this.events.push({
      t,
      model,
      project: row.cwd ? path.basename(row.cwd) : 'unknown',
      session: row.sessionId ?? null,
      input: usage.input_tokens ?? 0,
      cacheRead: usage.cache_read_input_tokens ?? 0,
      cacheWrite,
      output: usage.output_tokens ?? 0,
      webSearch: usage.server_tool_use?.web_search_requests ?? 0,
      cost: costUSD(model, usage),
    });
    return true;
  }

  #prune(added) {
    const cutoff = Date.now() - RETAIN_DAYS * 86_400_000;
    if (this.events.some((e) => e.t < cutoff)) {
      this.events = this.events.filter((e) => e.t >= cutoff);
    }
    // An id older than the retention window can only come back as a replay of
    // a row we would drop anyway, so the de-dup set is cut at the same point.
    for (const [id, t] of this.seen) if (t < cutoff) this.seen.delete(id);
    if (added) this.events.sort((a, b) => a.t - b.t);
  }
}
