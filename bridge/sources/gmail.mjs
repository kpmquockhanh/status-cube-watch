// Unread mail count, read over IMAP with a Gmail app password.
//
// Why IMAP and not the Gmail API: API OAuth needs a Google Cloud project whose
// refresh tokens expire after 7 days while it is in "testing" mode. An app
// password does not expire, and `STATUS INBOX (UNSEEN)` is one round trip.
// The client is a few lines of node:tls because the bridge has no dependencies.
//
// The credentials never leave the host -- the device only sees the count.
//
// Fails soft like limits.mjs: not configured, wrong password, offline all
// produce a result with an `error`, and the last good count is kept (marked
// stale) so a flaky connection does not blank the card.

import tls from 'node:tls';

const HOST = 'imap.gmail.com';
const PORT = 993;
const CACHE_MS = 60_000;
const TIMEOUT_MS = 8_000;

let cache = { at: 0, data: null };
let inFlight = null;

// IMAP quoted string. CR/LF cannot be quoted, so refuse them outright.
function quote(s) {
  if (/[\r\n]/.test(s)) throw new Error('bad characters in credentials');
  return `"${s.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
}

// One short-lived connection: LOGIN, STATUS, LOGOUT. Resolves to the number of
// unseen messages in the inbox.
function fetchUnseen(user, password) {
  return new Promise((resolve, reject) => {
    const sock = tls.connect({ host: HOST, port: PORT, servername: HOST });
    let buf = '';
    let step = 0;
    let unseen = null;
    let done = false;

    const finish = (err, value) => {
      if (done) return;
      done = true;
      clearTimeout(timer);
      sock.destroy();
      err ? reject(err) : resolve(value);
    };
    const timer = setTimeout(() => finish(new Error('mail timeout')), TIMEOUT_MS);
    sock.setEncoding('utf8');
    sock.on('error', (e) => finish(new Error(e.code === 'ENOTFOUND' ? 'mail offline' : e.message)));
    sock.on('close', () => finish(new Error('mail connection closed')));

    // Commands are sent one at a time as each reply completes; `step` is the
    // tag number we are waiting on (0 = server greeting).
    const send = (cmd) => sock.write(`a${step} ${cmd}\r\n`);

    const handleLine = (line) => {
      const m = /^\* STATUS .*\(UNSEEN (\d+)\)/i.exec(line);
      if (m) unseen = Number(m[1]);

      if (step === 0 && line.startsWith('* OK')) {
        step = 1;
        send(`LOGIN ${quote(user)} ${quote(password)}`);
      } else if (line.startsWith(`a${step} `)) {
        const ok = line.startsWith(`a${step} OK`);
        if (step === 1) {
          if (!ok) return finish(new Error('mail login failed'));
          step = 2;
          send('STATUS INBOX (UNSEEN)');
        } else if (step === 2) {
          if (!ok || unseen === null) return finish(new Error('mail status failed'));
          step = 3;
          send('LOGOUT');
        } else if (step === 3) {
          finish(null, unseen);
        }
      }
    };

    sock.on('data', (chunk) => {
      buf += chunk;
      let i;
      while ((i = buf.indexOf('\r\n')) >= 0) {
        const line = buf.slice(0, i);
        buf = buf.slice(i + 2);
        handleLine(line);
      }
    });
  });
}

// Shape: { unread: number|null, stale: bool, error: string|null }, or null when
// mail is not configured (the card is then left out entirely).
export async function getUnreadMail({ user, password }, now = Date.now()) {
  if (!user || !password) return null;
  if (cache.data && now - cache.at < CACHE_MS) return cache.data;
  // refresh() is on a 5s timer; share one request rather than stacking logins.
  inFlight ??= (async () => {
    try {
      // Google displays app passwords in groups separated by spaces.
      const unread = await fetchUnseen(user, password.replace(/\s+/g, ''));
      cache = { at: now, data: { unread, stale: false, error: null } };
    } catch (err) {
      const error = err.message ?? String(err);
      console.error('mail fetch failed:', error);
      // Back off for a minute either way; a wrong password must not hammer Google.
      cache = {
        at: now,
        data: { unread: cache.data?.unread ?? null, stale: cache.data?.unread != null, error },
      };
    } finally {
      inFlight = null;
    }
    return cache.data;
  })();
  return inFlight;
}
