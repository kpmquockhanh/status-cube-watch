// Package server is the bridge's HTTP side and refresh loop.
//
//	GET /api/status  -> the payload the firmware consumes
//	GET /            -> a browser mock of the device, for developing the UI
//	GET /health      -> liveness + last refresh result. Always 200 while the
//	                    process serves (the Mac helper reads any other status
//	                    as "no bridge"); a degraded bridge says so in the body.
//	POST /hook       -> a Claude Code hook event (loopback only, agents on)
//	POST /api/answer -> answer an agent's prompt (loopback only, agents on)
//
// Nothing on the LAN or in a browser may read prompt text or answer a prompt:
// /api/status serves the full payload (with the prompt) only to a request
// that passes loopbackRequest, and the two POSTs answer 404 to anything else.
package server

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
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
	Now     func() time.Time // nil = time.Now
	Preview func() []byte    // nil = preview.html from the working directory when present, else bridge.PreviewHTML
	Log     *log.Logger      // console.log; nil = stdout
	ErrLog  *log.Logger      // console.warn/error; nil = stderr
}

const (
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

	debounce, syncEvery time.Duration

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
	if d.Log == nil {
		d.Log = log.New(os.Stdout, "", 0)
	}
	if d.ErrLog == nil {
		d.ErrLog = log.New(os.Stderr, "", 0)
	}
	s := &Server{cfg: cfg, d: d, label: d.Source.Label(), hasRequestCounts: d.Source.HasRequestCounts(),
		debounce: agentsDebounce, syncEvery: agentsSyncEvery, hookErrs: map[string]bool{}}
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
	now := s.d.Now()
	if s.d.Agents == nil {
		p := cards.Build(s.in.events, o, now)
		s.full.Store(&p)
		s.lan.Store(&p)
		return
	}
	o.Agents, o.ShowAgents = s.d.Agents.Snapshot(), true
	lan := cards.Build(s.in.events, o, now)
	o.Full = true
	full := cards.Build(s.in.events, o, now)
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
func loopbackRequest(r *http.Request, port int) bool {
	if _, browser := r.Header["Origin"]; browser {
		return false
	}
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

func notFound(w http.ResponseWriter) { send(w, 404, []byte(`{"error":"not found"}`), jsonType) }

type errorBody struct {
	Error string `json:"error"`
}

func (s *Server) route(w http.ResponseWriter, r *http.Request) {
	switch r.URL.Path {
	case "/api/status":
		p := s.lan.Load()
		if s.loopback(r) {
			p = s.full.Load()
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
	case "/hook":
		s.hook(w, r)
	case "/api/answer":
		s.answer(w, r)
	default:
		notFound(w)
	}
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
	if !s.cubeClient(r) {
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
	err := s.d.Agents.Answer(r.Context(), a.Agent, a.Prompt, a.Key)
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
