// Package server is the bridge's HTTP side and refresh loop.
//
//	GET /api/status  -> the payload the firmware consumes
//	GET /            -> a browser mock of the device, for developing the UI
//	GET /board/      -> the web board: every agent, answer prompts, pick hats
//	GET /api/board   -> the web board's data (loopback only, agents on)
//	GET /health      -> liveness + last refresh result. Always 200 while the
//	                    process serves (the Mac helper reads any other status
//	                    as "no bridge"); a degraded bridge says so in the body.
//	POST /hook       -> a Claude Code hook event (loopback only, agents on)
//	POST /api/answer -> answer an agent's prompt (loopback only, agents on)
//	POST /api/hat -> set an agent session's hat (loopback only, agents on)
//	GET /api/changes -> wait for the agents to change: 204 as soon as they
//	                    do, 304 after changesWait (loopback only, agents on)
//
// Nothing on the LAN may read prompt text or answer a prompt: /api/status
// serves the full payload (with the prompt) only to a request that passes
// loopbackRequest, and the POSTs answer 404 to anything else. The one browser
// page that may is the bridge's own web board, opened on this Mac at
// http://127.0.0.1:<port>/board/ (boardRequest): /api/board, /api/answer,
// /api/hat and /api/changes accept it, /hook never does.
package server

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

	"claude-status-cube/bridge"
	"claude-status-cube/bridge/internal/agents"
	"claude-status-cube/bridge/internal/cards"
	"claude-status-cube/bridge/internal/config"
	"claude-status-cube/bridge/internal/limits"
	"claude-status-cube/bridge/internal/mail"
	"claude-status-cube/bridge/internal/sources"
)

// Deps are the server's inputs; tests swap them for stubs.
type Deps struct {
	Limits interface {
		Get(context.Context) limits.Limits
	}
	Mail interface {
		Get(context.Context, string, string) *mail.Mail
	}
	Source sources.Source
	// Agents is the agent tracker; nil when config Agents is off (no Agents
	// card, /hook and /api/answer answer 404).
	Agents interface {
		Hook([]byte) error
		Sync(context.Context)
		Answer(ctx context.Context, agentID, promptID, key string) error
		Snapshot() []agents.Agent
		Subscribe() (<-chan struct{}, func())
	}
	// Hats stores the per-session hat overrides; nil when agents are off.
	Hats interface {
		Get(folder string) (int, bool)
		Set(folder string, code *int) error
	}
	Now     func() time.Time // nil = time.Now
	Preview func() []byte    // nil = preview.html from the working directory when present, else bridge.PreviewHTML
	// BoardFile is one file of the web board (index.html, board.js, ...); nil =
	// board/<name> from the working directory when present, else bridge.Board.
	BoardFile func(name string) ([]byte, error)
	Log       *log.Logger // console.log; nil = stdout
	ErrLog    *log.Logger // console.warn/error; nil = stderr
}

const (
	// changesWait is how long GET /api/changes waits before answering 304,
	// inside the server's 10 s WriteTimeout.
	changesWait     = 8 * time.Second
	agentsDebounce  = 150 * time.Millisecond // tracker changes are gathered this long before a rebuild
	agentsSyncEvery = 3 * time.Second        // Tracker.Sync interval
	maxHookBody     = 8 << 20                // same cap as the hook command's stdin
	maxAnswerBody   = 4 << 10
	maxHookErrors   = 100 // distinct hook errors logged; later new ones are not
)

// inputs are what the last refresh read. A rebuild after a tracker change
// uses only these: it never reads limits or mail again, nor touches the
// Source, which is not goroutine-safe.
type inputs struct {
	lim    limits.Limits
	mail   *mail.Mail
	events []sources.Event // a copy
	billed *sources.Billed
	err    string // the source failed: the BRIDGE ERROR card
	extra  bool   // ExtraCards (off for the STARTING payload)
}

// Server holds the current payloads and the last refresh result.
type Server struct {
	cfg              config.Config
	d                Deps
	label            string // Source.Label(), constant: /health reads it without touching the source
	hasRequestCounts bool

	// full carries the Agents card's prompt and goes only to loopbackRequest;
	// lan is everyone else's. Without agents they are the same payload.
	full, lan atomic.Pointer[cards.Payload]
	// fullAgents is the snapshot the full payload was built from, so
	// ?detail=<id> finds its agent without touching the tracker.
	fullAgents atomic.Pointer[[]agents.Agent]

	debounce, syncEvery time.Duration

	// changed is closed (and replaced) on every agents rebuild and hat set:
	// a GET /api/changes waiting on it answers at once, so the Mac app pushes
	// a new status within a moment instead of at its next poll.
	changeMu    sync.Mutex
	changed     chan struct{}
	changesWait time.Duration

	mu          sync.Mutex // guards the fields below; held while a payload is built and stored
	in          inputs
	refreshed   bool
	lastError   string // the source (transcripts / Admin API) failed
	limitsError string // the rate-limit read failed
	lastOk      int64  // epoch ms

	hookErrMu   sync.Mutex
	hookErrs    map[string]bool // hook error messages already logged
	hookErrsCap bool            // maxHookErrors reached, said once
}

// New builds a server serving the STARTING payload until the first refresh.
func New(cfg config.Config, d Deps) *Server {
	if d.Now == nil {
		d.Now = time.Now
	}
	if d.Preview == nil {
		d.Preview = defaultPreview
	}
	if d.BoardFile == nil {
		d.BoardFile = defaultBoardFile
	}
	if d.Log == nil {
		d.Log = log.New(os.Stdout, "", 0)
	}
	if d.ErrLog == nil {
		d.ErrLog = log.New(os.Stderr, "", 0)
	}
	s := &Server{cfg: cfg, d: d, label: d.Source.Label(), hasRequestCounts: d.Source.HasRequestCounts(),
		debounce: agentsDebounce, syncEvery: agentsSyncEvery, hookErrs: map[string]bool{},
		changed: make(chan struct{}), changesWait: changesWait}
	// Served until the first refresh lands: the usage card with empty rings and
	// "starting" for a caption, so the cube has something valid to draw.
	s.in = inputs{lim: limits.Limits{Error: "starting up"}}
	s.publishLocked()
	return s
}

// publishLocked builds both payloads from s.in and the tracker's snapshot and
// stores them. Called with s.mu held (or before s is shared), so a rebuild
// can never store a payload older than the refresh that ran beside it.
func (s *Server) publishLocked() {
	lim := s.in.lim
	o := cards.Opts{
		Source:           s.label,
		HasRequestCounts: s.hasRequestCounts,
		Billed:           s.in.billed,
		Error:            s.in.err,
		ExtraCards:       s.in.extra,
		Limits:           &lim,
		Mail:             s.in.mail,
	}
	if s.d.Hats != nil {
		o.Hats = s.d.Hats.Get
	}
	now := s.d.Now()
	if s.d.Agents == nil {
		p := cards.Build(s.in.events, o, now)
		s.full.Store(&p)
		s.lan.Store(&p)
		return
	}
	o.Agents, o.ShowAgents = s.d.Agents.Snapshot(), true
	snap := o.Agents
	lan := cards.Build(s.in.events, o, now)
	o.Full = true
	full := cards.Build(s.in.events, o, now)
	s.fullAgents.Store(&snap)
	s.full.Store(&full)
	s.lan.Store(&lan)
}

// republish rebuilds both payloads from the last refresh's inputs.
func (s *Server) republish() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.publishLocked()
}

// defaultPreview re-reads preview.html from the working directory on every
// request, so a browser reload shows an edit; the binary's copy otherwise.
func defaultPreview() []byte {
	if b, err := os.ReadFile("preview.html"); err == nil {
		return b
	}
	return bridge.PreviewHTML
}

// defaultBoardFile is defaultPreview for the web board's files.
func defaultBoardFile(name string) ([]byte, error) {
	if b, err := os.ReadFile(filepath.Join("board", name)); err == nil {
		return b, nil
	}
	return fs.ReadFile(bridge.Board, "board/"+name)
}

// RefreshOnce runs one refresh. Limits and mail report their own error, so
// neither can take the other or the source down. A panic in either is
// re-raised here, on the caller's goroutine, like a rejected Promise.all in
// Node: the refresh loop logs it and the state is left as it was.
//
// It must not be called concurrently with itself or with Run: the Source is
// not goroutine-safe.
func (s *Server) RefreshOnce(ctx context.Context) {
	var (
		lim     limits.Limits
		m       *mail.Mail
		wg      sync.WaitGroup
		panics  [2]any
		capture = func(i int) {
			if v := recover(); v != nil {
				panics[i] = v
			}
			wg.Done()
		}
	)
	wg.Add(2)
	go func() { defer capture(0); lim = s.d.Limits.Get(ctx) }()
	go func() { defer capture(1); m = s.d.Mail.Get(ctx, s.cfg.GmailUser, s.cfg.GmailPassword) }()
	wg.Wait()
	for _, v := range panics {
		if v != nil {
			panic(v)
		}
	}

	// Transcript scanning only feeds the optional spend cards, so the default
	// deck never touches the source at all.
	var events []sources.Event
	var err error
	if s.cfg.ExtraCards {
		events, err = s.d.Source.Refresh(ctx)
	}
	// Read after Source.Refresh (the admin source sets Billed there), and here
	// on the refresh goroutine: a rebuild must not touch the source.
	in := inputs{lim: lim, mail: m, billed: s.d.Source.Billed(), extra: s.cfg.ExtraCards}
	if err == nil {
		in.events = slices.Clone(events)
	} else {
		in.err = err.Error()
		in.events = slices.Clone(s.d.Source.Events())
	}
	now := s.d.Now()

	func() {
		s.mu.Lock()
		defer s.mu.Unlock()
		s.limitsError = lim.Error
		s.lastError = in.err
		if err == nil {
			s.lastOk = now.UnixMilli()
		}
		s.in = in
		s.refreshed = true
		s.publishLocked()
	}()
	// A refresh cut short by shutdown is not a failure worth a log line.
	if err != nil && ctx.Err() == nil {
		s.d.ErrLog.Println("refresh failed:", err.Error())
	}
}

// watchAgents rebuilds the payloads soon after the tracker changes, so a new
// prompt reaches the cube without waiting for the next refresh: changes are
// gathered for s.debounce, then one rebuild from the last refresh's inputs.
func (s *Server) watchAgents(ctx context.Context) {
	ch, stop := s.d.Agents.Subscribe()
	defer stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ch:
		}
		t := time.NewTimer(s.debounce)
		select {
		case <-ctx.Done():
			t.Stop()
			return
		case <-t.C:
		}
		s.guard("agents rebuild failed:", s.republish)
		s.notifyChanged()
	}
}

// notifyChanged wakes every waiting GET /api/changes.
func (s *Server) notifyChanged() {
	s.changeMu.Lock()
	defer s.changeMu.Unlock()
	close(s.changed)
	s.changed = make(chan struct{})
}

// changes answers 204 as soon as the agents change (notifyChanged), 304 when
// changesWait passes first, and nothing when the client hangs up. Same guard
// as /api/answer; the Mac app calls it in a loop and fetches /api/status on 204.
func (s *Server) changes(w http.ResponseWriter, r *http.Request) {
	if !s.boardClient(r) {
		notFound(w)
		return
	}
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", http.MethodGet)
		sendJSON(w, 405, errorBody{"method not allowed"})
		return
	}
	s.changeMu.Lock()
	ch := s.changed
	s.changeMu.Unlock()
	t := time.NewTimer(s.changesWait)
	defer t.Stop()
	select {
	case <-ch:
		send(w, 204, nil, jsonType)
	case <-t.C:
		w.WriteHeader(304)
	case <-r.Context().Done():
	}
}

// syncLoop runs Tracker.Sync every s.syncEvery until ctx ends, on its own
// goroutine so slow tmux or `claude agents` calls never hold up the refresh.
func (s *Server) syncLoop(ctx context.Context) {
	for {
		s.guard("agents sync failed:", func() { s.d.Agents.Sync(ctx) })
		t := time.NewTimer(s.syncEvery)
		select {
		case <-ctx.Done():
			t.Stop()
			return
		case <-t.C:
		}
	}
}

// guard runs f, logging a panic instead of ending the process.
func (s *Server) guard(what string, f func()) {
	defer func() {
		if r := recover(); r != nil {
			s.d.ErrLog.Println(what, fmt.Sprint(r))
		}
	}()
	f()
}

// loop is a chain rather than a ticker: the next refresh starts only once
// this one has finished, so slow reads can never stack up.
func (s *Server) loop(ctx context.Context) {
	for {
		s.safeRefresh(ctx)
		t := time.NewTimer(time.Duration(s.cfg.RefreshMs) * time.Millisecond)
		select {
		case <-ctx.Done():
			t.Stop()
			return
		case <-t.C:
		}
	}
}

func (s *Server) safeRefresh(ctx context.Context) {
	defer func() {
		if r := recover(); r != nil {
			s.d.ErrLog.Println("refresh failed:", fmt.Sprint(r))
		}
	}()
	s.RefreshOnce(ctx)
}

// send is the only way a response is written. An explicit Content-Length
// keeps the response un-chunked: the device parses it with a very small HTTP
// client, so one framing less to handle.
func send(w http.ResponseWriter, code int, body []byte, contentType string) {
	h := w.Header()
	h.Set("Content-Length", strconv.Itoa(len(body)))
	h.Set("Content-Type", contentType)
	h.Set("Cache-Control", "no-store")
	w.WriteHeader(code)
	w.Write(body)
}

const jsonType = "application/json"

// marshal is json.Marshal without the \u003c escapes for <, > and &: they
// cost bytes on the BLE link and the Node bridge never wrote them.
func marshal(v any) ([]byte, error) {
	var buf bytes.Buffer
	enc := json.NewEncoder(&buf)
	enc.SetEscapeHTML(false)
	if err := enc.Encode(v); err != nil {
		return nil, err
	}
	return bytes.TrimSuffix(buf.Bytes(), []byte("\n")), nil
}

func sendJSON(w http.ResponseWriter, code int, v any) {
	b, err := marshal(v)
	if err != nil {
		panic(err)
	}
	send(w, code, b, jsonType)
}

type nullString string

func (n nullString) MarshalJSON() ([]byte, error) {
	if n == "" {
		return []byte("null"), nil
	}
	return marshal(string(n))
}

type healthBody struct {
	Ok          bool       `json:"ok"`
	Error       nullString `json:"error"`
	Source      string     `json:"source"`
	LastError   nullString `json:"lastError"`
	LimitsError nullString `json:"limitsError"`
	LastOk      int64      `json:"lastOk"`
}

func (s *Server) health() healthBody {
	s.mu.Lock()
	defer s.mu.Unlock()
	errMsg := s.lastError
	if errMsg == "" {
		errMsg = s.limitsError
	}
	if errMsg == "" && !s.refreshed {
		errMsg = "starting up"
	}
	return healthBody{
		Ok: errMsg == "", Error: nullString(errMsg), Source: s.label,
		LastError: nullString(s.lastError), LimitsError: nullString(s.limitsError), LastOk: s.lastOk,
	}
}

// loopbackRequest is the loopback guard: the connection comes from a loopback
// address, the Host header names this bridge exactly as 127.0.0.1:<port> or
// localhost:<port> (anything else may be a DNS-rebinding page), and there is
// no Origin header (a browser always sends one with a POST, and a page has no
// business reading the prompt either).
//
// Two accepted edge cases: a request in absolute form ("GET
// http://127.0.0.1:8787/api/status") takes r.Host from the URI, which only a
// local non-browser client can send; and the bridge's own preview page,
// opened at http://127.0.0.1:<port>/, gets the full payload, since a
// same-origin GET carries no Origin.
func loopbackRequest(r *http.Request, port int) bool {
	if _, browser := r.Header["Origin"]; browser {
		return false
	}
	return fromLoopback(r, port)
}

// boardRequest is loopbackRequest for the bridge's own web board: a browser
// may send Origin, but only the bridge's own (http:// + the Host the guard
// already pinned to 127.0.0.1:<port> or localhost:<port>), and a browser that
// says where the request comes from (Sec-Fetch-Site) must say same-origin.
// Any other page is cross-origin, and a write from it would also need a
// CORS preflight (X-Cube-Client) that the bridge never grants.
func boardRequest(r *http.Request, port int) bool {
	if !fromLoopback(r, port) {
		return false
	}
	if site := r.Header.Get("Sec-Fetch-Site"); site != "" && site != "same-origin" {
		return false
	}
	origin, browser := r.Header["Origin"]
	return !browser || (len(origin) == 1 && origin[0] == "http://"+r.Host)
}

// fromLoopback: the connection comes from a loopback address and Host names
// this bridge exactly.
func fromLoopback(r *http.Request, port int) bool {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if ip := net.ParseIP(host); err != nil || ip == nil || !ip.IsLoopback() {
		return false
	}
	p := strconv.Itoa(port)
	return r.Host == "127.0.0.1:"+p || r.Host == "localhost:"+p
}

// arrivedOn is the local port the request came in on; 0 when unknown, which
// no Host header matches, so the guard fails closed.
func arrivedOn(r *http.Request) int {
	if a, ok := r.Context().Value(http.LocalAddrContextKey).(*net.TCPAddr); ok {
		return a.Port
	}
	return 0
}

func (s *Server) loopback(r *http.Request) bool { return loopbackRequest(r, arrivedOn(r)) }

// cubeClient: agents are on and the request is the bridge's own hook command
// or the Mac helper (loopback guard plus X-Cube-Client: 1).
func (s *Server) cubeClient(r *http.Request) bool {
	return s.d.Agents != nil && r.Header.Get("X-Cube-Client") == "1" && s.loopback(r)
}

// boardClient is cubeClient that also lets in the bridge's own web board.
func (s *Server) boardClient(r *http.Request) bool {
	return s.d.Agents != nil && r.Header.Get("X-Cube-Client") == "1" && boardRequest(r, arrivedOn(r))
}

func notFound(w http.ResponseWriter) { send(w, 404, []byte(`{"error":"not found"}`), jsonType) }

type errorBody struct {
	Error string `json:"error"`
}

func (s *Server) route(w http.ResponseWriter, r *http.Request) {
	switch r.URL.Path {
	case "/api/status":
		p := s.lan.Load()
		var detail *cards.Payload
		if s.loopback(r) {
			p = s.full.Load()
			detail = s.withDetail(p, r.URL.Query().Get("detail"))
		}
		if detail != nil {
			p = detail
		}
		// cards.Marshal is what the payload budget measured.
		b, err := cards.Marshal(*p)
		if err != nil {
			panic(err)
		}
		send(w, 200, b, jsonType)
	case "/health":
		sendJSON(w, 200, s.health())
	case "/", "/index.html":
		send(w, 200, s.d.Preview(), "text/html; charset=utf-8")
	case "/board", "/board/", "/board/index.html", "/board/board.js", "/board/board.css", "/board/critter.js":
		s.boardFile(w, r)
	case "/api/board":
		s.board(w, r)
	case "/hook":
		s.hook(w, r)
	case "/api/answer":
		s.answer(w, r)
	case "/api/hat":
		s.hat(w, r)
	case "/api/changes":
		s.changes(w, r)
	default:
		notFound(w)
	}
}

// boardTypes are the web board's files and how they are served.
var boardTypes = map[string]string{
	"index.html": "text/html; charset=utf-8",
	"board.js":   "text/javascript; charset=utf-8",
	"critter.js": "text/javascript; charset=utf-8",
	"board.css":  "text/css; charset=utf-8",
}

// boardCSP keeps the board to its own files: no inline script, no other
// origin, no framing (a framed board could be clicked into answering).
const boardCSP = "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; " +
	"connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'"

// boardFile serves the web board's page and its scripts. They hold no data,
// so anyone may load them; /api/board decides who sees agents.
func (s *Server) boardFile(w http.ResponseWriter, r *http.Request) {
	name := filepath.Base(r.URL.Path)
	if name == "board" || name == "" {
		name = "index.html"
	}
	b, err := s.d.BoardFile(name)
	if err != nil {
		panic(err)
	}
	h := w.Header()
	h.Set("Content-Security-Policy", boardCSP)
	h.Set("X-Content-Type-Options", "nosniff")
	h.Set("Referrer-Policy", "no-referrer")
	send(w, 200, b, boardTypes[name])
}

// board serves every agent for the web board, to the board on this Mac only.
func (s *Server) board(w http.ResponseWriter, r *http.Request) {
	if s.d.Agents == nil {
		sendJSON(w, 404, errorBody{"agents are off"})
		return
	}
	if !boardRequest(r, arrivedOn(r)) {
		notFound(w)
		return
	}
	var hats func(string) (int, bool)
	if s.d.Hats != nil {
		hats = s.d.Hats.Get
	}
	sendJSON(w, 200, cards.BuildBoard(s.d.Agents.Snapshot(), s.d.Now(), hats))
}

// withDetail is the full payload with agent id's detail, or nil when id is
// empty or matches no agent of the snapshot the payload was built from.
func (s *Server) withDetail(full *cards.Payload, id string) *cards.Payload {
	snap := s.fullAgents.Load()
	if id == "" || snap == nil {
		return nil
	}
	for _, a := range *snap {
		if a.ID != id {
			continue
		}
		auto := true
		if s.d.Hats != nil {
			_, overridden := s.d.Hats.Get(cards.HatKey(a))
			auto = !overridden
		}
		p := cards.WithDetail(*full, a, auto, s.d.Now())
		return &p
	}
	return nil
}

// postOnly answers 405 to anything but POST and reports whether to go on.
func postOnly(w http.ResponseWriter, r *http.Request) bool {
	if r.Method == http.MethodPost {
		return true
	}
	w.Header().Set("Allow", http.MethodPost)
	sendJSON(w, 405, errorBody{"method not allowed"})
	return false
}

// hook hands a Claude Code hook event to the tracker. It always answers 204:
// a hook must never learn about the bridge's problems (they are logged).
func (s *Server) hook(w http.ResponseWriter, r *http.Request) {
	if !s.cubeClient(r) {
		notFound(w)
		return
	}
	if !postOnly(w, r) {
		return
	}
	body, err := io.ReadAll(http.MaxBytesReader(w, r.Body, maxHookBody))
	if err == nil {
		err = s.d.Agents.Hook(body)
	}
	if err != nil {
		s.logHookError(err)
	}
	send(w, 204, nil, jsonType)
}

// logHookError logs each distinct message once, up to maxHookErrors of them.
func (s *Server) logHookError(err error) {
	msg := err.Error()
	s.hookErrMu.Lock()
	defer s.hookErrMu.Unlock()
	switch {
	case s.hookErrs[msg]:
	case len(s.hookErrs) < maxHookErrors:
		s.hookErrs[msg] = true
		s.d.ErrLog.Println("hook failed:", msg)
	case !s.hookErrsCap:
		s.hookErrsCap = true
		s.d.ErrLog.Printf("hook failed: %d distinct errors logged, further ones are not", maxHookErrors)
	}
}

// answer types an answer to an agent's prompt into its tmux pane.
func (s *Server) answer(w http.ResponseWriter, r *http.Request) {
	if !s.boardClient(r) {
		notFound(w)
		return
	}
	if !postOnly(w, r) {
		return
	}
	var a struct {
		Agent  string `json:"agent"`
		Prompt string `json:"prompt"`
		Key    string `json:"key"`
	}
	dec := json.NewDecoder(http.MaxBytesReader(w, r.Body, maxAnswerBody))
	if err := dec.Decode(&a); err != nil || dec.Decode(&struct{}{}) != io.EOF ||
		a.Agent == "" || a.Prompt == "" || a.Key == "" {
		sendJSON(w, 400, errorBody{`want {"agent","prompt","key"}`})
		return
	}
	// Not cancelled when the client hangs up: a send cut off after the keys
	// landed would look failed, and the tracker could not tell it apart.
	err := s.d.Agents.Answer(context.WithoutCancel(r.Context()), a.Agent, a.Prompt, a.Key)
	switch {
	case err == nil:
		send(w, 204, nil, jsonType)
	case errors.Is(err, agents.ErrNotInTmux):
		sendJSON(w, 409, errorBody{"not in tmux"})
	case errors.Is(err, agents.ErrStale):
		sendJSON(w, 409, errorBody{err.Error()})
	case errors.Is(err, agents.ErrBadKey):
		sendJSON(w, 400, errorBody{err.Error()})
	case errors.Is(err, agents.ErrNotFound):
		sendJSON(w, 404, errorBody{err.Error()})
	default:
		s.d.ErrLog.Println("answer failed:", err.Error())
		sendJSON(w, 500, errorBody{err.Error()})
	}
}

// hat sets (or clears) the hat override of an agent's session (cards.HatKey).
// hat: 0..103 = that hat, -1 = no hat, null = automatic.
func (s *Server) hat(w http.ResponseWriter, r *http.Request) {
	if !s.boardClient(r) || s.d.Hats == nil {
		notFound(w)
		return
	}
	if !postOnly(w, r) {
		return
	}
	var b struct {
		Agent string          `json:"agent"`
		Hat   json.RawMessage `json:"hat"` // empty = key missing
	}
	dec := json.NewDecoder(http.MaxBytesReader(w, r.Body, maxAnswerBody))
	bad := dec.Decode(&b) != nil || dec.Decode(&struct{}{}) != io.EOF || b.Agent == "" || len(b.Hat) == 0
	var code *int
	if !bad && string(b.Hat) != "null" {
		var n int
		if err := json.Unmarshal(b.Hat, &n); err != nil || n < -1 || n > 103 {
			bad = true
		}
		code = &n
	}
	if bad {
		sendJSON(w, 400, errorBody{`want {"agent","hat": 0..103 | -1 | null}`})
		return
	}
	key := ""
	for _, a := range s.d.Agents.Snapshot() {
		if a.ID == b.Agent {
			key = cards.HatKey(a)
			break
		}
	}
	if key == "" {
		sendJSON(w, 404, errorBody{"no such agent"})
		return
	}
	if err := s.d.Hats.Set(key, code); err != nil {
		s.d.ErrLog.Println("hat failed:", err.Error())
		sendJSON(w, 500, errorBody{err.Error()})
		return
	}
	s.republish()
	s.notifyChanged()
	send(w, 204, nil, jsonType)
}

type trackingWriter struct {
	http.ResponseWriter
	wrote bool
}

func (t *trackingWriter) WriteHeader(code int) { t.wrote = true; t.ResponseWriter.WriteHeader(code) }
func (t *trackingWriter) Write(b []byte) (int, error) {
	t.wrote = true
	return t.ResponseWriter.Write(b)
}

// Handler serves every route. One odd request from anywhere on the LAN must
// not take the bridge down, so a panic becomes a 500.
func (s *Server) Handler() http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		tw := &trackingWriter{ResponseWriter: w}
		defer func() {
			if v := recover(); v != nil {
				if v == http.ErrAbortHandler {
					panic(v)
				}
				s.d.ErrLog.Printf("request %s failed: %v", r.URL.RequestURI(), v)
				if !tw.wrote {
					send(w, 500, []byte(`{"error":"internal error"}`), jsonType)
				}
			}
		}()
		s.route(tw, r)
	})
}

var loopbackHost = regexp.MustCompile(`^(127\.|::1$|localhost$)`)

// Run listens (plus 127.0.0.1 when Host is a pinned non-loopback address),
// then runs the refresh chain until ctx ends. It returns an error when the
// main listener cannot be opened.
func (s *Server) Run(ctx context.Context) error {
	host := s.cfg.Host
	wildcard := host == "" || host == "0.0.0.0" || host == "::"
	loopback := loopbackHost.MatchString(host)
	shown := host
	if shown == "" {
		shown = "*"
	}

	ln, err := net.Listen("tcp", net.JoinHostPort(host, strconv.Itoa(s.cfg.Port)))
	if err != nil {
		return fmt.Errorf("cannot listen on %s:%d: %w", shown, s.cfg.Port, err)
	}
	port := ln.Addr().(*net.TCPAddr).Port
	listeners := []net.Listener{ln}

	// A host pinned to one LAN address would shut out the Mac helper, which
	// always reads http://127.0.0.1:<port>. Losing it only costs the helper,
	// hence a warning, not an error.
	if !wildcard && !loopback {
		if local, err := net.Listen("tcp", net.JoinHostPort("127.0.0.1", strconv.Itoa(port))); err != nil {
			s.d.ErrLog.Printf("cannot also listen on 127.0.0.1:%d (%v); the Mac helper will not reach this bridge", port, err)
		} else {
			listeners = append(listeners, local)
		}
	}

	s.d.Log.Printf("claude-status-cube bridge  source=%s  refresh=%dms  host=%s", s.label, s.cfg.RefreshMs, shown)
	previewHost := host
	if wildcard {
		previewHost = "localhost"
	}
	s.d.Log.Printf("  preview   http://%s/", net.JoinHostPort(previewHost, strconv.Itoa(port)))
	var devices []string
	switch {
	case wildcard:
		devices = lanAddresses()
	case !loopback:
		devices = []string{host}
	}
	for _, ip := range devices {
		s.d.Log.Printf("  device    http://%s/api/status", net.JoinHostPort(ip, strconv.Itoa(port)))
	}

	srv := &http.Server{Handler: s.Handler(), ErrorLog: s.d.ErrLog, ReadHeaderTimeout: 10 * time.Second,
		WriteTimeout: 10 * time.Second, IdleTimeout: 30 * time.Second}
	var wg sync.WaitGroup
	for _, l := range listeners {
		wg.Add(1)
		go func() {
			defer wg.Done()
			if err := srv.Serve(l); err != nil && !errors.Is(err, http.ErrServerClosed) {
				s.d.ErrLog.Printf("serve %s: %v", l.Addr(), err)
			}
		}()
	}
	// Listen first and refresh in the background: the first usage read can
	// take seconds (Keychain, network), and nothing should be refused meanwhile.
	wg.Add(1)
	go func() { defer wg.Done(); s.loop(ctx) }()
	if s.d.Agents != nil {
		wg.Add(2)
		go func() { defer wg.Done(); s.watchAgents(ctx) }()
		go func() { defer wg.Done(); s.syncLoop(ctx) }()
	}

	<-ctx.Done()
	shutdown, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	srv.Shutdown(shutdown)
	wg.Wait()
	return nil
}

// lanAddresses are printed at startup so the right LAN address can be pasted
// into config.h.
func lanAddresses() []string {
	addrs, err := net.InterfaceAddrs()
	if err != nil {
		return nil
	}
	var out []string
	for _, a := range addrs {
		ipn, ok := a.(*net.IPNet)
		if !ok || ipn.IP.IsLoopback() {
			continue
		}
		if ip4 := ipn.IP.To4(); ip4 != nil {
			out = append(out, ip4.String())
		}
	}
	return out
}
