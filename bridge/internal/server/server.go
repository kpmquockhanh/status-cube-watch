// Package server is the bridge's HTTP side and refresh loop (server.mjs).
//
//	GET /api/status  -> the payload the firmware consumes
//	GET /            -> a browser mock of the device, for developing the UI
//	GET /health      -> liveness + last refresh result. Always 200 while the
//	                    process serves (the Mac helper reads any other status
//	                    as "no bridge"); a degraded bridge says so in the body.
package server

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"regexp"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

	"claude-status-cube/bridge"
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
	Source  sources.Source
	Now     func() time.Time // nil = time.Now
	Preview func() []byte    // nil = preview.html from the working directory when present, else bridge.PreviewHTML
	Log     *log.Logger      // console.log; nil = stdout
	ErrLog  *log.Logger      // console.warn/error; nil = stderr
}

// Server holds the current payload and the last refresh result.
type Server struct {
	cfg              config.Config
	d                Deps
	label            string // Source.Label(), constant: /health reads it without touching the source
	hasRequestCounts bool

	payload atomic.Pointer[cards.Payload]

	mu          sync.Mutex
	refreshed   bool
	lastError   string // the source (transcripts / Admin API) failed
	limitsError string // the rate-limit read failed
	lastOk      int64  // epoch ms
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
	s := &Server{cfg: cfg, d: d, label: d.Source.Label(), hasRequestCounts: d.Source.HasRequestCounts()}
	// Served until the first refresh lands: the usage card with empty rings and
	// "starting" for a caption, so the cube has something valid to draw.
	p := cards.Build(nil, cards.Opts{
		Source:           s.label,
		HasRequestCounts: s.hasRequestCounts,
		Limits:           &limits.Limits{Error: "starting up"},
	}, d.Now())
	s.payload.Store(&p)
	return s
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

	// Built after Source.Refresh: the admin source sets Billed there.
	common := func() cards.Opts {
		return cards.Opts{
			Source:           s.label,
			HasRequestCounts: s.hasRequestCounts,
			Billed:           s.d.Source.Billed(),
			ExtraCards:       s.cfg.ExtraCards,
			Limits:           &lim,
			Mail:             m,
		}
	}

	// Transcript scanning only feeds the optional spend cards, so the default
	// deck never touches the source at all.
	var events []sources.Event
	var err error
	if s.cfg.ExtraCards {
		events, err = s.d.Source.Refresh(ctx)
	}
	now := s.d.Now()

	var p cards.Payload
	s.mu.Lock()
	s.limitsError = lim.Error
	if err == nil {
		p = cards.Build(events, common(), now)
		s.lastError = ""
		s.lastOk = now.UnixMilli()
	} else {
		s.lastError = err.Error()
		o := common()
		o.Error = s.lastError
		p = cards.Build(s.d.Source.Events(), o, now)
	}
	s.refreshed = true
	s.mu.Unlock()
	if err != nil {
		s.d.ErrLog.Println("refresh failed:", err.Error())
	}
	s.payload.Store(&p)
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

func sendJSON(w http.ResponseWriter, code int, v any) {
	b, err := json.Marshal(v)
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
	return json.Marshal(string(n))
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

func (s *Server) route(w http.ResponseWriter, r *http.Request) {
	switch r.URL.Path {
	case "/api/status":
		sendJSON(w, 200, s.payload.Load())
	case "/health":
		sendJSON(w, 200, s.health())
	case "/", "/index.html":
		send(w, 200, s.d.Preview(), "text/html; charset=utf-8")
	default:
		send(w, 404, []byte(`{"error":"not found"}`), jsonType)
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

	srv := &http.Server{Handler: s.Handler(), ErrorLog: s.d.ErrLog, ReadHeaderTimeout: 10 * time.Second}
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
