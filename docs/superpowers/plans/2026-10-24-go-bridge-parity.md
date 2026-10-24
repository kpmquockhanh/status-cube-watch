# Go Bridge at Parity — Implementation Plan (1 of 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Node bridge (`bridge/*.mjs`) with a Go program, `cube-bridge`, that serves byte-for-byte-equivalent payloads, and switch `ClaudeCubeLink.app` and `bridge/agent.sh` over to it, so that no part of the repo needs `node` any more.

**Architecture:** One Go module rooted at `bridge/` (`module claude-status-cube/bridge`), a single binary `cmd/cube-bridge`, and one package per Node file. Each external read (limits, mail, admin, transcripts) is a struct with injectable clock, HTTP and dial hooks, so it can be tested without a network. `internal/cards` is checked against golden fixtures generated from the **Node** `cards.mjs` before the Node code is deleted; that is how parity is proved.

**Tech Stack:** Go ≥ 1.24, standard library plus `golang.org/x/text` (for NFKD only); Swift (mac-helper), bash.

**Spec:** `docs/superpowers/specs/2026-10-24-go-bridge-agents-design.md` (goal 1 and goal 9). Later plans: 2 = agent tracking, hooks and tmux answers in the bridge; 3 = the firmware's Agents card, prompt screen and alert; 4 = the BLE answer path and lock policy in the Mac helper.

## Global Constraints

- Go ≥ 1.24 (`go 1.24` in `go.mod`). This Mac has Go 1.23.0: before Task 1, ask the user to run `brew upgrade go` (or install 1.24+), and confirm with `go version`.
- Third-party Go modules: only `golang.org/x/text`. Nothing else.
- Binary name `cube-bridge`, built at `bridge/cube-bridge` (gitignored). No arguments = serve.
- Port 8787, endpoints `GET /api/status`, `/` (and `/index.html`), `/health`, LaunchAgent label `com.claude-status-cube.bridge`, log dir `~/Library/Logs/claude-status-cube/` all stay as they are.
- Config: `config.json` in the working directory (flag `-config <path>` overrides), env vars win over it with exactly the Node rules (see Task 1).
- Every HTTP response carries an explicit `Content-Length` and never `Transfer-Encoding: chunked`.
- All payload strings are printable ASCII cut to the firmware buffer sizes: card `t` 24, `v` 24, `s1` 40, `s2` 40, `m` 8; row `k` 6, `p` 8, `r` 12 (bytes including NUL, so one less character).
- Thresholds 60 / 85 (`amber` / `red`) unchanged.
- Timeouts: Keychain 3 s, usage endpoint 4 s, IMAP 8 s, Admin API 15 s per page. Caches 60 s. Limits stale for at most 10 min. 429 back-off clamped to 5–30 min.
- The payload contract (`{v, ts, src, cards:[…]}`) is unchanged: firmware, `preview.html` and the Mac helper's parsing are not touched by this plan.

## Review Focus

1. **`config.json` with loose types**: `"port": "8787"`, `"extraCards": "yes"`, `"refreshMs": 5000.0` all work today; a typed Go struct would reject them. Expect them to be read as Node reads them. *(Task 1 test `TestLoadLooseTypes`.)*
2. **A transcript line far longer than 64 KB** (pasted images are base64 in the JSONL): a `bufio.Scanner` with default limits silently stops at the first one. Expect every later line still counted. *(Task 6 test `TestHugeLine`.)*
3. **A JSONL line written half-way** when the scan lands, including a multi-byte character cut in two: expect it skipped now and counted once, whole, on the next scan. *(Task 6 test `TestPartialLine`.)*
4. **Rounding at exact halves**: JS `toFixed` rounds 2.5 → "3" and 1.25 → "1.3"; Go's `strconv` rounds half to even ("2", "1.2"). Expect the JS result on every formatted number. *(Task 3 test `TestToFixedMatchesJS`, plus golden cases.)*
5. **A payload bigger than net/http's buffer** (extra cards plus an error card): Go would switch to chunked encoding, which the cube's tiny HTTP client cannot read. Expect `Content-Length` always. *(Task 8 test `TestLargeResponseNotChunked`.)*

---

## File map

| File | Responsibility |
|---|---|
| `bridge/go.mod`, `bridge/go.sum` | module `claude-status-cube/bridge`, `go 1.24`, `golang.org/x/text` |
| `bridge/assets.go` | `package bridge`: `//go:embed preview.html` → `var PreviewHTML []byte` |
| `bridge/cmd/cube-bridge/main.go` | flags, config load, wiring, `Run` |
| `bridge/internal/config/config.go` | `Config`, `Load` (Node's `loadConfig` + admin floor) |
| `bridge/internal/pricing/pricing.go` | `Usage`, `CostUSD`, `Rates` (port of `pricing.mjs`) |
| `bridge/internal/sources/source.go` | `Event`, `Billed`, `Source` interface |
| `bridge/internal/sources/local.go` | transcript reader (port of `sources/local.mjs`) |
| `bridge/internal/sources/admin.go` | Admin API (port of `sources/admin.mjs`) |
| `bridge/internal/limits/limits.go` | OAuth usage reader (port of `limits.mjs`) |
| `bridge/internal/mail/gmail.go` | IMAP unread count (port of `sources/gmail.mjs`) |
| `bridge/internal/cards/format.go` | `toFixed`, `money`, `tokens`, `ago`, `until`, `prettyModel`, `Fit` |
| `bridge/internal/cards/cards.go` | `Card`, `Payload`, `Opts`, `Build` (port of `cards.mjs`) |
| `bridge/internal/cards/testdata/gen.mjs` | generates golden cases from the Node `cards.mjs` (kept after Node is gone, with its output) |
| `bridge/internal/server/server.go` | HTTP handler, refresh loop, listeners, health |
| `bridge/tools/parity.sh` | runs Node and Go side by side and diffs `/api/status` (deleted with Node) |

Each `*.go` above has a `*_test.go` beside it.

---

### Task 1: Module scaffold and config

**Files:**
- Create: `bridge/go.mod`, `bridge/assets.go`, `bridge/cmd/cube-bridge/main.go` (stub: loads config, prints it, exits), `bridge/internal/config/config.go`, `bridge/internal/config/config_test.go`
- Modify: `.gitignore` (add `bridge/cube-bridge`)

**Interfaces:**
- Produces:
  ```go
  package config
  type Config struct {
      Source        string // "local" | "admin"
      Port          int
      Host          string // "" = every interface
      RefreshMs     int
      AdminKey      string
      OAuthToken    string
      ExtraCards    bool
      GmailUser     string
      GmailPassword string
  }
  // lookup is os.LookupEnv in production; file is config.json's bytes (nil = absent or unreadable).
  func Load(lookup func(string) (string, bool), file []byte, warn func(string)) Config
  func ReadFile(path string) []byte // nil on any error
  ```
  `package bridge` exports `var PreviewHTML []byte`.

- [ ] **Step 1: Confirm Go ≥ 1.24.** Run `go version`. Expected: `go1.24` or later. If not, stop and ask the user to upgrade.

- [ ] **Step 2: Write the failing tests** in `config_test.go`:
  - `TestDefaults`: no env, no file → `{Source:"local", Port:8787, Host:"", RefreshMs:5000, ExtraCards:false}`.
  - `TestAdminDefaultsAndFloor`: `CUBE_SOURCE=admin` → `RefreshMs == 60000`; `CUBE_SOURCE=admin, CUBE_REFRESH_MS=5000` → `60000` and one warning containing `60s`.
  - `TestEnvBeatsFileBothWays`: file `{"extraCards": true}` + `CUBE_EXTRA_CARDS=0` → false; file `{"extraCards": false}` + `CUBE_EXTRA_CARDS=yes` → true.
  - `TestEmptyEnvIsUnset`: `CUBE_PORT=""` with file `{"port": 9000}` → 9000; `CUBE_HOST=""` with file `{"host":"10.0.0.2"}` → `"10.0.0.2"`.
  - `TestSecretsUseNullish`: `ANTHROPIC_ADMIN_KEY=""` (set, empty) with file `{"adminKey":"k"}` → `""` (Node uses `??` for the secrets and the gmail pair, so an empty env value wins); unset → `"k"`. Gmail from `{"gmail":{"user":"u","appPassword":"p"}}`.
  - `TestBadNumberFallsBack`: `CUBE_PORT=abc` → 8787 and a warning containing `port`; `CUBE_REFRESH_MS=500` → 5000 (min 1000); `CUBE_PORT=70000` → 8787; `CUBE_PORT=80.5` → 8787 (integer required).
  - `TestLoadLooseTypes` (Review Focus 1): file `{"port":"8788","refreshMs":2000.0,"extraCards":"on","host":""}` → Port 8788, RefreshMs 2000, ExtraCards true, Host "".
  - `TestBadBoolWarns`: `CUBE_EXTRA_CARDS=maybe` → false, one warning.
  - `TestBrokenFile`: file `{not json` → defaults, no panic.

- [ ] **Step 3: Run to verify they fail.** `cd bridge && go test ./internal/config/` → FAIL (undefined `Load`).

- [ ] **Step 4: Implement `Load`.** Decode the file into `map[string]any` (so loose types survive). `num(name, raw any, def, min, max, integer)` accepts a JSON number or a numeric string (`strconv.ParseFloat`) and warns `config: ignoring <name>=<raw> (want an integer|a number <min>..<max>), using <def>` otherwise. `bool(name, raws ...any)` takes the first non-empty of env then file: Go `bool` as is; strings `1/true/yes/on` and `0/false/no/off`, case-insensitive and trimmed; anything else warns and moves on; none → false. Env lookups for `CUBE_SOURCE`, `CUBE_PORT`, `CUBE_HOST`, `CUBE_REFRESH_MS`, `CUBE_EXTRA_CARDS` treat `""` as unset; `ANTHROPIC_ADMIN_KEY`, `ANTHROPIC_ADMIN_OAUTH_TOKEN`, `CUBE_GMAIL_USER`, `CUBE_GMAIL_PASSWORD` count when set at all. Refresh default 60000 for admin, else 5000; range 1000..86400000. Admin floor: if `Source=="admin" && RefreshMs < 60000`, warn `admin source: raising refresh interval to the documented 60s minimum` and set 60000.

- [ ] **Step 5: Scaffold.** `go.mod` with `module claude-status-cube/bridge` and `go 1.24`; `assets.go` embedding `preview.html`; `main.go` that parses `-config` (default `config.json`), calls `config.Load(os.LookupEnv, config.ReadFile(path), func(s string){ log.Println(s) })` and prints the result (replaced in Task 8). Add `bridge/cube-bridge` to `.gitignore`.

- [ ] **Step 6: Run tests and build.** `go test ./... && go build -o cube-bridge ./cmd/cube-bridge` → PASS, binary built.

- [ ] **Step 7: Commit.** `git add bridge/go.mod bridge/assets.go bridge/cmd bridge/internal/config .gitignore && git commit -m "feat(bridge): Go module scaffold and config loading"`

---

### Task 2: Pricing

**Files:**
- Create: `bridge/internal/pricing/pricing.go`, `pricing_test.go`

**Interfaces:**
- Produces:
  ```go
  package pricing
  type CacheCreation struct {
      Ephemeral5m int64 `json:"ephemeral_5m_input_tokens"`
      Ephemeral1h int64 `json:"ephemeral_1h_input_tokens"`
  }
  // Usage is a transcript line's message.usage, decoded straight from JSON.
  type Usage struct {
      InputTokens              int64          `json:"input_tokens"`
      CacheReadInputTokens     int64          `json:"cache_read_input_tokens"`
      CacheCreationInputTokens int64          `json:"cache_creation_input_tokens"`
      OutputTokens             int64          `json:"output_tokens"`
      CacheCreation            *CacheCreation `json:"cache_creation"` // nil = no split recorded
      Speed                    string         `json:"speed"`
      InferenceGeo             string         `json:"inference_geo"`
      ServerToolUse            *struct{ WebSearchRequests int64 `json:"web_search_requests"` } `json:"server_tool_use"`
  }
  type Rate struct { In, Out, Read float64; Fast *struct{ In, Out float64 } } // Read 0 = default 0.1
  func Rates(model string) Rate
  func CostUSD(model string, u Usage) float64
  ```

- [ ] **Step 1: Write the failing tests:**
  - `TestRatesLongestPrefix`: `Rates("claude-opus-5-5").In == 4`; `Rates("claude-opus-5").In == 5`; `Rates("claude-haiku-4-5-20251001").In == 1`; `Rates("claude-opus-4-0").In == 15`; `Rates("claude-opus-5-55").In == 2` (no `-` boundary → fallback {2,10}); `Rates("").In == 2`.
  - `TestCost`: `CostUSD("claude-sonnet-4-5", Usage{InputTokens:1e6})` = 3.0; cache read 1e6 on sonnet-4-5 = 0.3; on opus-5-5 = 0.2 (read 0.05); `CacheCreation{Ephemeral5m:1e6}` on sonnet-4-5 = 3.75 and `Ephemeral1h:1e6` = 6.0; no split with `CacheCreationInputTokens:1e6` = 3.75; `Speed:"fast"` on opus-5-5 with 1e6 output = 40, on opus-4-6 = 25 (no fast entry); `InferenceGeo:"us"` multiplies by 1.1. Compare with a 1e-9 tolerance.

- [ ] **Step 2: Run to verify they fail.** `go test ./internal/pricing/` → FAIL.

- [ ] **Step 3: Implement.** Copy the table from `bridge/pricing.mjs` verbatim (all 19 ids, with `read` and `fast` where given), fallback `{In:2, Out:10}`, ids sorted by length descending for the prefix match, match only when the next char is `-`.

- [ ] **Step 4: Run tests.** PASS.

- [ ] **Step 5: Commit.** `git commit -m "feat(bridge): Go pricing table"`

---

### Task 3: Cards, checked against the Node output

**Files:**
- Create: `bridge/internal/cards/testdata/gen.mjs`, `bridge/internal/cards/testdata/cases-utc.json`, `bridge/internal/cards/testdata/cases-hcm.json` (generated), `bridge/internal/sources/source.go`, `bridge/internal/cards/format.go`, `format_test.go`, `cards.go`, `cards_test.go`
- Stub types used here that Tasks 4–5 fill: create `bridge/internal/limits/types.go` and `bridge/internal/mail/types.go` with only the types below.

**Interfaces:**
- Consumes: none from earlier tasks.
- Produces:
  ```go
  package sources
  type Event struct {
      T                                          int64 // epoch ms
      Model, Project, Session                    string // "" = null in Node
      Input, CacheRead, CacheWrite, Output, WebSearch int64
      Cost                                       float64
  }
  type Billed struct { ByDay map[string]float64; Total float64 } // key YYYY-MM-DD
  type Source interface {
      Label() string
      Refresh(ctx context.Context) ([]Event, error)
      Events() []Event   // last good events, after a failed Refresh too
      HasRequestCounts() bool
      Billed() *Billed   // nil unless admin
  }

  package limits
  type Window struct { Utilization *float64 `json:"utilization"`; ResetsAt string `json:"resets_at"` }
  type Limits struct { FiveHour, SevenDay *Window; Error string } // Error "" = none

  package mail
  type Mail struct { Unread *int; Stale bool; Error string }

  package cards
  type Row struct { K string `json:"k"`; P string `json:"p"`; R string `json:"r"` }
  type Card struct {
      T  string `json:"t"`;  V  string `json:"v"`;  S1 string `json:"s1"`; S2 string `json:"s2"`
      C  string `json:"c"`;  G  *int   `json:"g,omitempty"`
      C2 string `json:"c2,omitempty"`; G2 *int `json:"g2,omitempty"`
      Rows []Row `json:"rows,omitempty"`
      M  string `json:"m,omitempty"`;  MC string `json:"mc,omitempty"`
  }
  type Payload struct { V int `json:"v"`; Ts int64 `json:"ts"`; Src string `json:"src"`; Cards []Card `json:"cards"` }
  type Opts struct {
      Source string; HasRequestCounts bool; Billed *sources.Billed; Error string
      Limits *limits.Limits; Mail *mail.Mail; ExtraCards bool
  }
  // now carries the local zone: today/month boundaries use now.Location().
  func Build(events []sources.Event, o Opts, now time.Time) Payload
  func Fit(s string, bytes int) string
  ```

- [ ] **Step 1: Write the generator `testdata/gen.mjs`.** It imports `../../../cards.mjs`, reads no files, and prints a JSON array of `{name, now, events, opts, want}`. To pin time it swaps the global `Date` per case for a subclass whose zero-argument constructor and `Date.now()` return the case's `now`, then restores it:
  ```js
  const Real = Date;
  function at(now, fn) {
    globalThis.Date = class extends Real {
      constructor(...a) { super(...(a.length ? a : [now])); }
      static now() { return now; }
    };
    try { return fn(); } finally { globalThis.Date = Real; }
  }
  ```
  `opts.billed` is written as `{byDay: {...}, total}` and turned into a `Map` before calling `buildPayload`. Fix `now = Date.UTC(2026, 9, 24, 15, 30)` unless a case needs otherwise. The cases (each a name plus the minimal input):
  - usage: `72%/31%` with resets in 2.5 h / 80 h; utilization `72.5` (rounds to 73); exactly `59`, `60`, `84`, `85`; 5h lapsed (reset 1 min ago) with 7d live; `resets_at` missing; both windows null with each error: `no Claude Code login found`, `starting up`, `usage endpoint timeout`, `usage endpoint 429`, `usage endpoint bad JSON`, `ECONNRESET`; a window with `utilization: "12"` (a string → treated as no reading); countdowns of 47 min, 4 h 05 min, 16 h, 2 d 3 h.
  - mail: absent; `{unread: null}`; `{unread: 3, stale: true}`; `{unread: 0}`; `{unread: 1234}`.
  - extra cards (`extraCards: true`): events across two models (`claude-opus-5-5`, `claude-haiku-4-5-20251001`, and one with `model: null`), two projects, some yesterday, some earlier this month and one last month; with and without `billed` (today present in `byDay`, and absent); `hasRequestCounts: false` with `source: 'admin'`; no events at all; costs chosen to hit each `money` branch (0.5, 150, 1500, 15000) and token totals hitting each `tokens` branch including exact halves (2500 → "3K", 1250000 → "1.3M"); a last event 90 s / 2 h / 3 d ago.
  - error card: `error` of 60 characters (cut to 28).
  - fit: a project named `Café · résumé — “quoted” dự-án đ…×` (and the same text as a mail-less usage error) to exercise the look-alike table and NFKD.
  
  Generate with `cd bridge/internal/cards/testdata && TZ=UTC node gen.mjs > cases-utc.json && TZ=Asia/Ho_Chi_Minh node gen.mjs > cases-hcm.json`. In the HCM run, also add one case with `now = Date.UTC(2026, 9, 24, 17, 30)` (00:30 local next day) so "today" differs between zones.

- [ ] **Step 2: Write the failing tests.**
  - `cards_test.go: TestGolden` loads both files; for each case it rebuilds `events`/`opts` as Go values, runs `Build` with `now = time.UnixMilli(c.now).In(loc)` (`time.UTC` or `time.LoadLocation("Asia/Ho_Chi_Minh")`), marshals the result, and compares it with `want` as parsed JSON (`reflect.DeepEqual` on `any`), reporting the case name and both JSONs on a mismatch.
  - `format_test.go: TestToFixedMatchesJS` (Review Focus 4): `toFixed(2.5,0)=="3"`, `toFixed(1.25,1)=="1.3"`, `toFixed(0.125,2)=="0.13"`, `toFixed(1.005,2)=="1.00"`, `toFixed(1234.5678,2)=="1234.57"`, `toFixed(0,2)=="0.00"`.
  - `format_test.go: TestFit`: `Fit("Café", 24)=="Cafe"`; `Fit("a\tb\nc", 24)=="a b c"`; `Fit("đ·–…×", 24)=="d--...x"`; `Fit(strings.Repeat("x",40), 24)` has length 23; `Fit("ab  ", 4)=="ab"` (trimEnd after the cut); `Fit("日本", 24)==""`.

- [ ] **Step 3: Run to verify they fail.** `go test ./internal/cards/` → FAIL.

- [ ] **Step 4: Implement `format.go`.** `toFixed(x float64, d int) string` computes the exact decimal: `big.Float` at precision 256 set from `x`, multiplied by `10^d`, plus 0.5, floored to a `big.Int` `n` (this picks the larger `n` on a tie, as ECMAScript requires), then formatted with `d` decimals. `money`, `tokens`, `ago`, `until`, `prettyModel` port their JS counterparts using `toFixed` wherever JS uses `.toFixed`, `math.Round` for `Math.round` (inputs are non-negative), and `regexp` for `prettyModel`'s two patterns. Thousands separators for the mail count: group digits by 3 with `,`. `Fit` follows `cards.mjs::fit` in order: look-alike map (`· • – — − ' ' " " … × đ Đ ø Ø ł Ł ß æ Æ`), `norm.NFKD`, every Unicode whitespace → space, drop everything outside 0x20–0x7E, keep the first `bytes-1` bytes, trim trailing spaces.

- [ ] **Step 5: Implement `cards.go`.** A direct port of `usageCard`, `windowView`, `shortReason`, `mailBadge`, `spendCards`, `totals`, `topBy` (stable sort by value descending, ties in first-seen order like JS `sort`), `buildPayload` and `fitCard`. `ts = now.Unix()`. A window counts only when `Utilization != nil`. Give `limits.Window` (in `limits/types.go`) an `UnmarshalJSON` that leaves `Utilization` nil when the value is not a JSON number, instead of failing the whole document; the golden test decodes its `limits` input through it, and Task 4 decodes the endpoint's body through it. `Model == ""` reads as `unknown` in `topBy` and `prettyModel`.

- [ ] **Step 6: Run tests.** `go test ./internal/cards/` → PASS, every golden case equal.

- [ ] **Step 7: Commit.** `git add bridge/internal && git commit -m "feat(bridge): Go card builder, golden-tested against cards.mjs"`

---

### Task 4: Usage limits

**Files:**
- Create: `bridge/internal/limits/limits.go`, `limits_test.go` (types from Task 3 stay in `types.go`)

**Interfaces:**
- Consumes: `limits.Limits`, `limits.Window` (Task 3).
- Produces:
  ```go
  type Reader struct {
      URL    string                               // default https://api.anthropic.com/api/oauth/usage
      Client *http.Client                         // default: 4 s timeout
      Token  func(ctx context.Context) string     // default: Keychain, then ~/.claude/.credentials.json
      Now    func() time.Time
  }
  func NewReader() *Reader
  func (r *Reader) Get(ctx context.Context) Limits
  func ParseToken(raw string) string // "" when none
  ```

- [ ] **Step 1: Write the failing tests** (an `httptest.Server`, a fake clock, `Token` returning `"tok"`):
  - `TestParseToken`: `{"claudeAiOauth":{"accessToken":"a"}}`→`a`; `{"accessToken":"b"}`→`b`; `sk-ant-x`→`sk-ant-x`; `garbage`→`""`.
  - `TestOK`: the server checks `Authorization: Bearer tok`, `anthropic-beta: oauth-2025-04-20` and `Accept: application/json`, returns `{"five_hour":{"utilization":42,"resets_at":"…"},"seven_day":null}` → `FiveHour.Utilization==42`, `SevenDay==nil`, `Error==""`.
  - `TestCache60s`: two `Get`s 59 s apart → 1 request; at 61 s → 2.
  - `TestNoLogin`: `Token` returns `""` → `Error=="no Claude Code login found"`, no request, and no new attempt before 60 s.
  - `Test429Backoff`: 429 with `Retry-After: 60` → `Error=="usage endpoint 429"`, the next request not before 5 min; with `Retry-After: 7200` → not before 30 min; without the header → 5 min.
  - `TestOtherStatus`: 500 → `Error=="usage endpoint 500"`, retry after 60 s.
  - `TestStaleThenEmpty`: a good read, then failures: at +5 min the old windows come back with `Error` set; at +11 min both windows nil.
  - `TestBadJSON`: body `{` → `Error=="usage endpoint bad JSON"`. `TestTimeout`: a handler that sleeps past the client timeout (set the client to 50 ms in the test) → `Error=="usage endpoint timeout"`.

- [ ] **Step 2: Run to verify they fail.** FAIL.

- [ ] **Step 3: Implement** following `limits.mjs` line by line: state `cache {at, data}`, `retryAt`, `lastError`; the `failed(err)` rule (last good data within 10 min, else empty windows). The default `Token` runs `security find-generic-password -s "Claude Code-credentials" -w` under a 3 s `exec.CommandContext` (macOS only), then reads `~/.claude/.credentials.json`.

- [ ] **Step 4: Run tests.** PASS.

- [ ] **Step 5: Commit.** `git commit -m "feat(bridge): Go usage-limits reader"`

---

### Task 5: Gmail unread count

**Files:**
- Create: `bridge/internal/mail/gmail.go`, `gmail_test.go`

**Interfaces:**
- Consumes: `mail.Mail` (Task 3).
- Produces:
  ```go
  type Reader struct {
      Dial func(ctx context.Context) (net.Conn, error) // default: tls.Dial to imap.gmail.com:993, ServerName imap.gmail.com
      Now  func() time.Time
  }
  func NewReader() *Reader
  // nil when user or password is empty (no badge).
  func (r *Reader) Get(ctx context.Context, user, password string) *Mail
  ```

- [ ] **Step 1: Write the failing tests** with `Dial` returning one end of `net.Pipe()` and a goroutine playing the server:
  - `TestUnseen`: greeting `* OK ready`, expects `a1 LOGIN "u" "pw"` (the password `"p w"` arrives with its spaces removed as `"pw"`), replies `a1 OK`; expects `a2 STATUS INBOX (UNSEEN)`, replies `* STATUS INBOX (UNSEEN 7)` and `a2 OK`; expects `a3 LOGOUT`, replies `a3 OK` → `Unread==7`, `Stale==false`.
  - `TestQuote`: user `a"b\c` is sent as `"a\"b\\c"`; a password containing `\r` → `Error=="bad characters in credentials"`, nothing sent after the greeting.
  - `TestLoginFails`: `a1 NO` → `Error=="mail login failed"`, `Unread==nil`.
  - `TestStaleKeepsCount`: a good 7, then (61 s later) a failure → `Unread==7, Stale==true`, `Error` set.
  - `TestCacheAndBackoff`: a failure at t, a second `Get` at t+30 s does not dial.
  - `TestTimeout`: a server that never greets, with the timeout set to 50 ms → `Error=="mail timeout"`.
  - `TestNotConfigured`: empty user → `nil`.

- [ ] **Step 2: Run to verify they fail.** FAIL.

- [ ] **Step 3: Implement** as `gmail.mjs`: one connection per fetch, lines split on `\r\n`, tag `a<step>`, the `* STATUS … (UNSEEN n)` regex, 8 s overall deadline (`conn.SetDeadline`; make it a field `Timeout` defaulting to 8 s so the test can shorten it), a `sync.Mutex` so concurrent callers share one fetch, and the 60 s cache that also backs off after a failure. `ENOTFOUND` → `mail offline` (a `*net.DNSError`).

- [ ] **Step 4: Run tests.** PASS.

- [ ] **Step 5: Commit.** `git commit -m "feat(bridge): Go Gmail unread reader"`

---

### Task 6: Local transcript source

**Files:**
- Create: `bridge/internal/sources/local.go`, `local_test.go`

**Interfaces:**
- Consumes: `sources.Event`, `sources.Source` (Task 3), `pricing.Usage`, `pricing.CostUSD` (Task 2).
- Produces: `func NewLocal(root string) *Local` (root default `~/.claude/projects` when `""`), implementing `Source` with `Label()=="local"`, `HasRequestCounts()==true`, `Billed()==nil`; plus `Now func() time.Time` for pruning.

- [ ] **Step 1: Write the failing tests** (a temp root; lines are `{"timestamp":…,"requestId":…,"cwd":"/x/proj","sessionId":"s","message":{"model":"claude-sonnet-4-5","usage":{…}}}`):
  - `TestReadsRecursively`: one file in `p/a.jsonl` and one in `p/sess/subagents/b.jsonl` → 2 events; `Project=="proj"`, `Cost` equals `pricing.CostUSD`.
  - `TestDedup`: the same `requestId` twice (across two files) → 1 event; a line without `requestId` falls back to `message.id`, then `uuid`.
  - `TestIncremental`: refresh, append a line, refresh → only the new line is parsed (count events = 2, and the first file's offset equals its old size before the append).
  - `TestPartialLine` (Review Focus 3): write a line without `\n` whose text ends inside a multi-byte character, refresh → 0 events; finish the line, refresh → 1 event with the right `Project` (`"projé"`).
  - `TestHugeLine` (Review Focus 2): a 2 MB line (a `content` string) without `usage`, then a normal line → 1 event, and a second normal line appended later is counted too.
  - `TestTruncated`: a file rewritten shorter → re-read from 0.
  - `TestUnchangedSkipped`: same size and mtime → the file is not opened (count `open`s through an injectable `open func(string) (*os.File, error)` field, default `os.Open`).
  - `TestPrune`: an event 36 days old (by `Now`) is dropped, and its `requestId` can be counted again later.
  - `TestDeletedFile`: a file removed between refreshes disappears from the offset table.
  - `TestCacheWriteSplit`: `cache_creation` present → `CacheWrite = 5m + 1h`; absent → `cache_creation_input_tokens`.

- [ ] **Step 2: Run to verify they fail.** FAIL.

- [ ] **Step 3: Implement** as `local.mjs`: `filepath.WalkDir` for `*.jsonl`; per file `{offset, size, mtime}`; read `[from, size)` in 1 MiB chunks, splitting on raw `\n` bytes with a carry buffer of unbounded size (no `bufio.Scanner`); decode each complete line only if it starts with `{`; `offset` = position after the last `\n`; dedup map `id → t`; 35-day prune of events and ids; sort events by `T` when any were added. Missing `timestamp` → `Now()`. `Project = filepath.Base(cwd)` or `unknown`.

- [ ] **Step 4: Run tests.** PASS.

- [ ] **Step 5: Commit.** `git commit -m "feat(bridge): Go local transcript source"`

---

### Task 7: Admin API source

**Files:**
- Create: `bridge/internal/sources/admin.go`, `admin_test.go`

**Interfaces:**
- Produces: `func NewAdmin(adminKey, oauthToken string) (*Admin, error)` (error `admin source needs ANTHROPIC_ADMIN_KEY or ANTHROPIC_ADMIN_OAUTH_TOKEN` when both are empty), fields `Base string` (default `https://api.anthropic.com/v1/organizations`), `Client *http.Client` (15 s timeout), `Now func() time.Time`; `Label()=="admin"`, `HasRequestCounts()==false`, `Billed()` set by `Refresh`.

- [ ] **Step 1: Write the failing tests** against an `httptest.Server`:
  - `TestHeaders`: with a key → `x-api-key`, no `Authorization`; with an OAuth token → `Authorization: Bearer …` and no `x-api-key`; both carry `anthropic-version: 2023-06-01` and `user-agent: claude-status-cube/1.0`.
  - `TestUsageQuery`: `/usage_report/messages` gets `bucket_width=1h`, `limit=168`, `group_by[]` = `model`, `workspace_id`, `inference_geo` (in that order), `starting_at` = now − 168 h.
  - `TestPagination`: page 1 `has_more:true,next_page:"p2"`, page 2 `has_more:false` → events from both; the loop stops at 20 pages.
  - `TestEventsAndBilled`: usage rows map to events (`Project` = `workspace_id` or `default`, `Session` empty, cost via pricing with `inference_geo`); `cost_report` rows with `amount:"1234"` sum to 12.34 USD per `starting_at[:10]`, `Total` the sum; `cost_report` asked with `bucket_width=1d`, `limit=31`, `group_by[]=description`, `starting_at` = UTC midnight of the local 1st.
  - `TestHTTPError`: 401 with body → error text starts with `/usage_report/messages -> 401`.

- [ ] **Step 2: Run to verify they fail.** FAIL.

- [ ] **Step 3: Implement** as `admin.mjs`, fetching both reports concurrently and sorting events by `T`.

- [ ] **Step 4: Run tests.** PASS.

- [ ] **Step 5: Commit.** `git commit -m "feat(bridge): Go Admin API source"`

---

### Task 8: Server, refresh loop and main

**Files:**
- Create: `bridge/internal/server/server.go`, `server_test.go`
- Modify: `bridge/cmd/cube-bridge/main.go`

**Interfaces:**
- Consumes: everything above.
- Produces:
  ```go
  package server
  type Deps struct {
      Limits interface{ Get(context.Context) limits.Limits }
      Mail   interface{ Get(context.Context, string, string) *mail.Mail }
      Source sources.Source
      Now    func() time.Time
      Preview func() []byte // dev: preview.html from the working directory when present, else bridge.PreviewHTML
  }
  type Server struct{ /* … */ }
  func New(cfg config.Config, d Deps) *Server
  func (s *Server) Handler() http.Handler
  func (s *Server) RefreshOnce(ctx context.Context)
  // Listens (plus 127.0.0.1 when Host is a pinned non-loopback address), then runs the refresh chain until ctx ends.
  // Returns an error when the main listener cannot be opened.
  func (s *Server) Run(ctx context.Context) error
  ```

- [ ] **Step 1: Write the failing tests** with stub `Deps`:
  - `TestStartingPayload`: before any refresh, `/api/status` has one card with `s1=="starting"`, `g==-1`, `g2==-1`.
  - `TestStatusAfterRefresh`: after `RefreshOnce`, the card reflects the stub limits; response `Content-Type: application/json`, `Cache-Control: no-store`, and `Content-Length` equal to the body length.
  - `TestLargeResponseNotChunked` (Review Focus 5): `Preview` returns 64 KB; fetch `/` and `/api/status` (extra cards on, an error set) with a real `httptest.Server` → for both, `resp.TransferEncoding` is empty and `resp.ContentLength` equals the body length. (Both go through the same `send`, so the 64 KB page proves the path a later, larger payload will take.)
  - `TestHealth`: always 200; body `{ok, error, source, lastError, limitsError, lastOk}` with `error=="starting up"` before the first refresh, `null`s after a good one, `lastError` set when `Source.Refresh` fails, `limitsError` from the stub limits.
  - `TestSourceErrorCard`: `ExtraCards` on and `Refresh` returning `errors.New("boom")` → first card `t=="BRIDGE ERROR"`, `s1=="boom"`, and the source's previous `Events()` still feed the spend cards.
  - `TestExtraCardsOffSkipsSource`: `ExtraCards` off → `Refresh` never called.
  - `TestPreviewAndNotFound`: `/` and `/index.html` return the preview with `text/html; charset=utf-8`; `/nope` → 404 `{"error":"not found"}`.
  - `TestPanicIs500`: a `Preview` func that panics → 500 `{"error":"internal error"}`, and the next request still works.
  - `TestBusyPort`: `Run` on a port already held by a test listener returns an error.
  - `TestNoOverlap`: a `Source.Refresh` that blocks for 300 ms with `RefreshMs` = 100 is never entered twice concurrently (count with an atomic).

- [ ] **Step 2: Run to verify they fail.** FAIL.

- [ ] **Step 3: Implement.** The current payload lives in an `atomic.Pointer[cards.Payload]` and is marshalled per request; every response goes through one `send(w, code, body, contentType)` that sets `Content-Length` before writing. A recover middleware turns panics into the 500 JSON. The refresh chain is a goroutine: refresh (limits and mail in parallel via two goroutines and a `WaitGroup`; then the source only when `ExtraCards`), then `time.Sleep(RefreshMs)`, until `ctx` is done. Startup log lines match Node's: `claude-status-cube bridge  source=<label>  refresh=<ms>ms  host=<host|*>`, `  preview   http://…/`, and one `  device    http://<ip>:<port>/api/status` per LAN IPv4 when listening on every interface.

- [ ] **Step 4: Wire `main.go`.** Load config; build `sources.NewAdmin` or `sources.NewLocal("")` (an admin config without a key logs the error and exits 1, as Node does); `server.New(...).Run(ctx)` with `ctx` cancelled on SIGINT/SIGTERM; exit 1 when `Run` returns an error.

- [ ] **Step 5: Run tests and the binary.** `go test ./... && go build -o cube-bridge ./cmd/cube-bridge && CUBE_PORT=8799 ./cube-bridge` then `curl -si localhost:8799/api/status | head -5` → `HTTP/1.1 200`, a `Content-Length` header, no `Transfer-Encoding`; `curl -s localhost:8799/health`.

- [ ] **Step 6: Commit.** `git commit -m "feat(bridge): Go HTTP server and refresh loop"`

---

### Task 9: Live parity check and the LaunchAgent

**Files:**
- Create: `bridge/tools/parity.sh`
- Modify: `bridge/agent.sh`

**Interfaces:**
- Consumes: the `cube-bridge` binary (Task 8).

- [ ] **Step 1: Write `tools/parity.sh`.** It starts `node server.mjs` on port 8801 and `./cube-bridge` on 8802 with the same environment (`CUBE_EXTRA_CARDS` passed through), waits until both `/health` answer and have refreshed (`error` is not `starting up`), fetches `/api/status` from each, drops `ts`, and diffs them with `diff <(…) <(…)` after normalising through `plutil -convert json -r -o - -` (no `jq`/`node` needed for the comparison itself), then stops both. Exit status = the diff's.

- [ ] **Step 2: Run it twice.** `./tools/parity.sh` and `CUBE_EXTRA_CARDS=1 ./tools/parity.sh` → no diff, exit 0. (The countdown can tick between the two fetches; if it does, rerun. Persistent diffs are bugs to fix in the owning task.)

- [ ] **Step 3: Switch `agent.sh` to the binary.** `install` runs `go build -o "$DIR/cube-bridge" ./cmd/cube-bridge` (failing with `go not found: install Go 1.24+` when `go` is missing), and the plist's `ProgramArguments` becomes just `$x_dir/cube-bridge` with `WorkingDirectory` unchanged. `read_config` uses `/usr/bin/plutil -extract <key> raw -o - "$DIR/config.json"` instead of `node -e`. `status_agent` prints `curl -fsS …/health` instead of the node-formatted card lines. Drop `pick_node` and every `CUBE_NODE` mention. Update the header comment ("reload after editing cards.mjs" → "rebuild and reload after editing the Go code").

- [ ] **Step 4: Verify.** `./agent.sh install && ./agent.sh status` → `state = running`, `serving on port 8787`. Then `./agent.sh uninstall` if the user does not normally run the LaunchAgent (ask before installing over a running Mac helper: the two must not both serve 8787).

- [ ] **Step 5: Commit.** `git commit -m "feat(bridge): LaunchAgent runs the Go bridge; parity script"`

---

### Task 10: The Mac helper runs the Go bridge

**Files:**
- Modify: `mac-helper/Sources/CubeLinkCore/BridgeSupervisor.swift`, `mac-helper/Sources/ClaudeCubeLink/main.swift`, `mac-helper/Sources/CubeLinkCore/BridgeSettings.swift` (comments only: `server.mjs` → `cube-bridge`), `mac-helper/Tests/CubeLinkCoreTests/SupervisorTests.swift`, `mac-helper/build-app.sh`, `mac-helper/install.sh`

**Interfaces:**
- Produces:
  ```swift
  public enum SupervisorAction: Equatable { case adopt; case spawn(binary: String); case fail(String) }
  public static func decide(externalBridgeUp: Bool, binary: String?) -> SupervisorAction
  /// First of CUBE_BRIDGE_DIR, a recorded bridgeDir, then bridge/ under each searchFrom (and up to 4 parents) that holds `cube-bridge`.
  public static func findBridgeDir(env: [String: String], recorded: String?, searchFrom: [String], exists: (String) -> Bool) -> String?
  public init(bridgeDir: URL, port: Int, client: BridgeClient? = nil, childEnvironment: …, log: …)
  ```
  `findNode` and the `node` init parameter are removed.

- [ ] **Step 1: Update the tests first.** In `SupervisorTests.swift`: `decide(externalBridgeUp: true, binary: nil) == .adopt`; `decide(false, binary: "/b/cube-bridge") == .spawn(binary: "/b/cube-bridge")`; `decide(false, binary: nil)` is `.fail` whose text contains `install.sh install`; the `findBridgeDir` tests look for `…/bridge/cube-bridge` instead of `server.mjs`; delete the `findNode` tests.

- [ ] **Step 2: Run.** `cd mac-helper && swift test` → FAIL (signatures).

- [ ] **Step 3: Implement.** `spawn` runs `<bridgeDir>/cube-bridge` with no arguments, cwd `bridgeDir`, env as before plus `CUBE_PORT`. `main.swift` computes `binary = bridgeDir + "/cube-bridge"` if executable, drops `findNode`/`nodePath`, and logs `bridge binary <path|not found>`. The fail text: `bridge binary not found: rebuild the app with mac-helper/install.sh install, or set CUBE_BRIDGE_DIR`.

- [ ] **Step 4: Bundle the binary.** `build-app.sh`: replace the `*.mjs` copy with `(cd ../bridge && GOOS=darwin GOARCH=arm64 go build -trimpath -o "$PWD/$APP/Contents/Resources/bridge/cube-bridge" ./cmd/cube-bridge)` (check `command -v go` first, with the same error text as `agent.sh`), stop copying `package.json`/`preview.html`/`config.example.json` (the preview is embedded), and `codesign --force --sign "$IDENTITY" --identifier com.claude-cube.bridge "$APP/Contents/Resources/bridge/cube-bridge"` before signing the app. `install.sh`: drop `CUBE_NODE` from the plist and the `nodePath` write; keep `defaults delete … nodePath` in both `record_paths` and `uninstall` so old installs are cleaned; update the header comment (`Node bridge` → `Go bridge`).

- [ ] **Step 5: Verify.** `swift test` → PASS. `./build-app.sh` → `built …`, and `codesign --verify --deep --strict build/ClaudeCubeLink.app` → no output. Ask the user before `./install.sh install` (it needs a terminal for the Bluetooth prompt); after it, `./install.sh logs` shows `started bridge (pid …)` and `curl -s 127.0.0.1:8787/health` answers.

- [ ] **Step 6: Commit.** `git commit -m "feat(mac-helper): supervise the Go bridge binary instead of node"`

---

### Task 11: Remove Node and update the docs

**Files:**
- Delete: `bridge/server.mjs`, `bridge/cards.mjs`, `bridge/limits.mjs`, `bridge/pricing.mjs`, `bridge/sources/*.mjs`, `bridge/package.json`, `bridge/tools/parity.sh`
- Keep: `bridge/internal/cards/testdata/gen.mjs` and its JSON output (header comment: "generated once from the Node bridge at <commit>; the JSON is now the reference")
- Modify: `CLAUDE.md` (root), `bridge/CLAUDE.md`, `README.md`, `bridge/config.example.json` (comments: `cards.mjs` → `internal/cards`)

- [ ] **Step 1: Delete the Node files** and confirm nothing still points at them: `grep -rn "server.mjs\|cards.mjs\|limits.mjs\|pricing.mjs\|gmail.mjs\|local.mjs\|admin.mjs\|CUBE_NODE\|nodePath" --exclude-dir=.git --exclude-dir=.superpowers --exclude-dir=docs .` → only intended mentions (old plans under `docs/` are history and stay).

- [ ] **Step 2: Rewrite `bridge/CLAUDE.md` for Go**, keeping its content: commands (`go test ./...`, `go build -o cube-bridge ./cmd/cube-bridge && ./cube-bridge`, `CUBE_PORT=8788`, `curl …`, `./agent.sh restart` after a rebuild, the Mac helper only picks up changes after `../mac-helper/install.sh install`), how to try a layout without a server (a Go test calling `cards.Build`, or `go run` of a tiny program, piped to `cube-shot`), the server rules (listen first, `STARTING`, the refresh chain, three independent reads, `Content-Length`, recover, the loopback listener), the fail-soft rules with the same timeouts, the `Source` interface and `Event` fields, the `cards` notes (`Fit` tables in `cards.go` must match `payload.h`, `tone()` thresholds, `windowView`, `until`, `shortReason`), and the pricing notes. Note the golden test and how to add a case (edit the Go test's case list now that Node is gone).

- [ ] **Step 3: Update root `CLAUDE.md`.** "`bridge/` — Node ≥20 ESM, **zero npm dependencies**" → "Go ≥ 1.24, one module dependency (`golang.org/x/text`)"; commands block (`go build … && ./cube-bridge`, `CUBE_SOURCE=admin ./cube-bridge`); "All formatting lives in `bridge/cards.mjs`" → `bridge/internal/cards`; the Mac helper paragraph (the bundled copy is the compiled `cube-bridge`, run directly; `install.sh` no longer records `nodePath`); the data-flow paragraph's file names. Keep every rule, only the file names and commands change.

- [ ] **Step 4: Update `README.md`**: the setup steps (install Go 1.24+, build, run), the LaunchAgent section (no node, no nvm caveat), the Mac helper section (`CUBE_NODE` gone), the file tree (`server.mjs` lines → the Go layout).

- [ ] **Step 5: Final verification.** `cd bridge && go vet ./... && go test ./...` → PASS; `cd ../mac-helper && swift test` → PASS; `cd ../firmware/sim && make shot` against the Go bridge running on 8787 → the usage card PNG looks the same as before (compare with `build/state-0.png`).

- [ ] **Step 6: Commit.** `git commit -m "chore(bridge): remove the Node bridge; docs for the Go bridge"`
