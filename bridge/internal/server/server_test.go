package server

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"io"
	"log"
	"net"
	"net/http"
	"net/http/httptest"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"claude-status-cube/bridge/internal/cards"
	"claude-status-cube/bridge/internal/config"
	"claude-status-cube/bridge/internal/limits"
	"claude-status-cube/bridge/internal/mail"
	"claude-status-cube/bridge/internal/sources"
)

type stubLimits struct {
	mu sync.Mutex
	l  limits.Limits
}

func (s *stubLimits) Get(context.Context) limits.Limits {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.l
}

type stubMail struct{}

func (stubMail) Get(context.Context, string, string) *mail.Mail { return nil }

type stubSource struct {
	calls   atomic.Int32
	refresh func(ctx context.Context) ([]sources.Event, error)
	events  []sources.Event
}

func (s *stubSource) Label() string { return "stub" }
func (s *stubSource) Refresh(ctx context.Context) ([]sources.Event, error) {
	s.calls.Add(1)
	if s.refresh != nil {
		return s.refresh(ctx)
	}
	return s.events, nil
}
func (s *stubSource) Events() []sources.Event { return s.events }
func (s *stubSource) HasRequestCounts() bool  { return true }
func (s *stubSource) Billed() *sources.Billed { return nil }

var quiet = log.New(io.Discard, "", 0)

func f64(v float64) *float64 { return &v }

var fixedNow = time.Date(2026, 10, 24, 12, 0, 0, 0, time.UTC)

func newTest(cfg config.Config, src *stubSource, l limits.Limits) (*Server, *stubLimits) {
	if src == nil {
		src = &stubSource{}
	}
	lim := &stubLimits{l: l}
	s := New(cfg, Deps{
		Limits:  lim,
		Mail:    stubMail{},
		Source:  src,
		Now:     func() time.Time { return fixedNow },
		Preview: func() []byte { return []byte("<html>preview</html>") },
		Log:     quiet,
		ErrLog:  quiet,
	})
	return s, lim
}

func get(t *testing.T, h http.Handler, path string) *httptest.ResponseRecorder {
	t.Helper()
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, httptest.NewRequest("GET", path, nil))
	return rec
}

func payloadOf(t *testing.T, rec *httptest.ResponseRecorder) cards.Payload {
	t.Helper()
	var p cards.Payload
	if err := json.Unmarshal(rec.Body.Bytes(), &p); err != nil {
		t.Fatalf("bad payload %q: %v", rec.Body.String(), err)
	}
	return p
}

func goodLimits() limits.Limits {
	return limits.Limits{
		FiveHour: &limits.Window{Utilization: f64(42), ResetsAt: "2026-10-24T14:00:00Z"},
		SevenDay: &limits.Window{Utilization: f64(10), ResetsAt: "2026-10-28T00:00:00Z"},
	}
}

func TestStartingPayload(t *testing.T) {
	s, _ := newTest(config.Config{RefreshMs: 5000}, nil, goodLimits())
	p := payloadOf(t, get(t, s.Handler(), "/api/status"))
	if len(p.Cards) != 1 {
		t.Fatalf("cards = %d, want 1", len(p.Cards))
	}
	c := p.Cards[0]
	if c.S1 != "starting" || c.G == nil || *c.G != -1 || c.G2 == nil || *c.G2 != -1 {
		t.Fatalf("starting card = %+v", c)
	}
	if p.Src != "stub" {
		t.Fatalf("src = %q, want stub", p.Src)
	}
}

func TestStatusAfterRefresh(t *testing.T) {
	s, _ := newTest(config.Config{RefreshMs: 5000}, nil, goodLimits())
	s.RefreshOnce(context.Background())
	rec := get(t, s.Handler(), "/api/status")
	c := payloadOf(t, rec).Cards[0]
	if *c.G != 42 || *c.G2 != 10 {
		t.Fatalf("g=%d g2=%d, want 42/10", *c.G, *c.G2)
	}
	h := rec.Header()
	if h.Get("Content-Type") != "application/json" || h.Get("Cache-Control") != "no-store" {
		t.Fatalf("headers = %v", h)
	}
	if h.Get("Content-Length") != strconv.Itoa(rec.Body.Len()) {
		t.Fatalf("Content-Length %q, body %d", h.Get("Content-Length"), rec.Body.Len())
	}
}

func TestLargeResponseNotChunked(t *testing.T) {
	big := bytes.Repeat([]byte("x"), 64*1024)
	src := &stubSource{refresh: func(context.Context) ([]sources.Event, error) { return nil, errors.New("boom") }}
	s := New(config.Config{RefreshMs: 5000, ExtraCards: true}, Deps{
		Limits:  &stubLimits{l: goodLimits()},
		Mail:    stubMail{},
		Source:  src,
		Now:     func() time.Time { return fixedNow },
		Preview: func() []byte { return big },
		Log:     quiet,
		ErrLog:  quiet,
	})
	s.RefreshOnce(context.Background())
	ts := httptest.NewServer(s.Handler())
	defer ts.Close()
	for _, path := range []string{"/", "/api/status"} {
		resp, err := http.Get(ts.URL + path)
		if err != nil {
			t.Fatal(err)
		}
		body, _ := io.ReadAll(resp.Body)
		resp.Body.Close()
		if len(resp.TransferEncoding) != 0 {
			t.Errorf("%s: Transfer-Encoding %v", path, resp.TransferEncoding)
		}
		if resp.ContentLength != int64(len(body)) {
			t.Errorf("%s: ContentLength %d, body %d", path, resp.ContentLength, len(body))
		}
	}
}

type health struct {
	Ok          bool    `json:"ok"`
	Error       *string `json:"error"`
	Source      string  `json:"source"`
	LastError   *string `json:"lastError"`
	LimitsError *string `json:"limitsError"`
	LastOk      int64   `json:"lastOk"`
}

func healthOf(t *testing.T, h http.Handler) health {
	t.Helper()
	rec := get(t, h, "/health")
	if rec.Code != 200 {
		t.Fatalf("health status %d", rec.Code)
	}
	var raw map[string]json.RawMessage
	if err := json.Unmarshal(rec.Body.Bytes(), &raw); err != nil {
		t.Fatal(err)
	}
	for _, k := range []string{"ok", "error", "source", "lastError", "limitsError", "lastOk"} {
		if _, ok := raw[k]; !ok {
			t.Fatalf("health missing %q: %s", k, rec.Body)
		}
	}
	var out health
	json.Unmarshal(rec.Body.Bytes(), &out)
	return out
}

func TestHealth(t *testing.T) {
	src := &stubSource{}
	s, lim := newTest(config.Config{RefreshMs: 5000, ExtraCards: true}, src, goodLimits())
	h := healthOf(t, s.Handler())
	if h.Ok || h.Error == nil || *h.Error != "starting up" || h.Source != "stub" || h.LastOk != 0 ||
		h.LastError != nil || h.LimitsError != nil {
		t.Fatalf("before refresh: %+v", h)
	}

	s.RefreshOnce(context.Background())
	h = healthOf(t, s.Handler())
	if !h.Ok || h.Error != nil || h.LastError != nil || h.LimitsError != nil || h.LastOk != fixedNow.UnixMilli() {
		t.Fatalf("after good refresh: %+v", h)
	}

	src.refresh = func(context.Context) ([]sources.Event, error) { return nil, errors.New("disk gone") }
	lim.mu.Lock()
	lim.l = limits.Limits{Error: "HTTP 401"}
	lim.mu.Unlock()
	s.RefreshOnce(context.Background())
	h = healthOf(t, s.Handler())
	if h.Ok || h.Error == nil || *h.Error != "disk gone" || h.LastError == nil || *h.LastError != "disk gone" ||
		h.LimitsError == nil || *h.LimitsError != "HTTP 401" || h.LastOk != fixedNow.UnixMilli() {
		t.Fatalf("after failures: %+v", h)
	}

	src.refresh = nil
	s.RefreshOnce(context.Background())
	h = healthOf(t, s.Handler())
	if h.Ok || h.Error == nil || *h.Error != "HTTP 401" || h.LastError != nil {
		t.Fatalf("limits error only: %+v", h)
	}
}

func TestSourceErrorCard(t *testing.T) {
	ev := sources.Event{T: fixedNow.Add(-time.Hour).UnixMilli(), Model: "claude-opus-4", Project: "cube",
		Input: 1000, Output: 500, Cost: 1.25}
	src := &stubSource{
		events:  []sources.Event{ev},
		refresh: func(context.Context) ([]sources.Event, error) { return nil, errors.New("boom") },
	}
	s, _ := newTest(config.Config{RefreshMs: 5000, ExtraCards: true}, src, goodLimits())
	s.RefreshOnce(context.Background())
	p := payloadOf(t, get(t, s.Handler(), "/api/status"))
	if p.Cards[0].T != "BRIDGE ERROR" || p.Cards[0].S1 != "boom" {
		t.Fatalf("first card = %+v", p.Cards[0])
	}
	var today *cards.Card
	for i := range p.Cards {
		if p.Cards[i].T == "TODAY" {
			today = &p.Cards[i]
		}
	}
	if today == nil || today.V != "$1.25" {
		t.Fatalf("TODAY card from previous events = %+v", today)
	}
}

func TestExtraCardsOffSkipsSource(t *testing.T) {
	src := &stubSource{}
	s, _ := newTest(config.Config{RefreshMs: 5000}, src, goodLimits())
	s.RefreshOnce(context.Background())
	s.RefreshOnce(context.Background())
	if n := src.calls.Load(); n != 0 {
		t.Fatalf("Refresh called %d times", n)
	}
	if p := payloadOf(t, get(t, s.Handler(), "/api/status")); len(p.Cards) != 1 {
		t.Fatalf("cards = %d, want 1", len(p.Cards))
	}
}

func TestPreviewAndNotFound(t *testing.T) {
	s, _ := newTest(config.Config{RefreshMs: 5000}, nil, goodLimits())
	for _, path := range []string{"/", "/index.html"} {
		rec := get(t, s.Handler(), path)
		if rec.Code != 200 || rec.Body.String() != "<html>preview</html>" ||
			rec.Header().Get("Content-Type") != "text/html; charset=utf-8" {
			t.Fatalf("%s: %d %q %v", path, rec.Code, rec.Body, rec.Header())
		}
	}
	rec := get(t, s.Handler(), "/nope")
	if rec.Code != 404 || rec.Body.String() != `{"error":"not found"}` {
		t.Fatalf("/nope: %d %q", rec.Code, rec.Body)
	}
}

func TestPanicIs500(t *testing.T) {
	var calls atomic.Int32
	s := New(config.Config{RefreshMs: 5000}, Deps{
		Limits: &stubLimits{l: goodLimits()},
		Mail:   stubMail{},
		Source: &stubSource{},
		Now:    func() time.Time { return fixedNow },
		Preview: func() []byte {
			if calls.Add(1) == 1 {
				panic("kaboom")
			}
			return []byte("ok")
		},
		Log:    quiet,
		ErrLog: quiet,
	})
	rec := get(t, s.Handler(), "/")
	if rec.Code != 500 || rec.Body.String() != `{"error":"internal error"}` {
		t.Fatalf("panic: %d %q", rec.Code, rec.Body)
	}
	if rec := get(t, s.Handler(), "/"); rec.Code != 200 || rec.Body.String() != "ok" {
		t.Fatalf("after panic: %d %q", rec.Code, rec.Body)
	}
}

func TestBusyPort(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	port := ln.Addr().(*net.TCPAddr).Port
	s, _ := newTest(config.Config{Host: "127.0.0.1", Port: port, RefreshMs: 5000}, nil, goodLimits())
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if err := s.Run(ctx); err == nil {
		t.Fatal("Run on a busy port returned nil")
	}
}

func TestNoOverlap(t *testing.T) {
	var inside, maxInside atomic.Int32
	src := &stubSource{}
	src.refresh = func(context.Context) ([]sources.Event, error) {
		n := inside.Add(1)
		for {
			m := maxInside.Load()
			if n <= m || maxInside.CompareAndSwap(m, n) {
				break
			}
		}
		time.Sleep(300 * time.Millisecond)
		inside.Add(-1)
		return nil, nil
	}
	s, _ := newTest(config.Config{Host: "127.0.0.1", Port: 0, RefreshMs: 100, ExtraCards: true}, src, goodLimits())
	ctx, cancel := context.WithTimeout(context.Background(), 1200*time.Millisecond)
	defer cancel()
	if err := s.Run(ctx); err != nil {
		t.Fatal(err)
	}
	if src.calls.Load() < 2 {
		t.Fatalf("refresh ran %d times, want at least 2", src.calls.Load())
	}
	if m := maxInside.Load(); m != 1 {
		t.Fatalf("max concurrent refreshes = %d", m)
	}
}

type panicLimits struct{ n atomic.Int32 }

func (p *panicLimits) Get(context.Context) limits.Limits {
	if p.n.Add(1) == 1 {
		panic("limits exploded")
	}
	return goodLimits()
}

type panicMail struct{ n atomic.Int32 }

func (p *panicMail) Get(context.Context, string, string) *mail.Mail {
	if p.n.Add(1) == 1 {
		panic("mail exploded")
	}
	return nil
}

// A panic in the limits or mail reader runs on its own goroutine; it must
// reach the refresh loop's recover instead of killing the process.
func TestReaderPanicIsLogged(t *testing.T) {
	for _, tc := range []struct {
		name string
		lim  interface {
			Get(context.Context) limits.Limits
		}
		mail interface {
			Get(context.Context, string, string) *mail.Mail
		}
		want string
	}{
		{"limits", &panicLimits{}, stubMail{}, "refresh failed: limits exploded"},
		{"mail", &stubLimits{l: goodLimits()}, &panicMail{}, "refresh failed: mail exploded"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var buf bytes.Buffer
			s := New(config.Config{RefreshMs: 5000}, Deps{
				Limits: tc.lim, Mail: tc.mail, Source: &stubSource{},
				Now: func() time.Time { return fixedNow },
				Log: quiet, ErrLog: log.New(&buf, "", 0),
			})
			s.safeRefresh(context.Background())
			if !strings.Contains(buf.String(), tc.want) {
				t.Fatalf("log = %q, want %q", buf.String(), tc.want)
			}
			if h := healthOf(t, s.Handler()); h.Error == nil || *h.Error != "starting up" {
				t.Fatalf("after panic: %+v", h)
			}
			s.safeRefresh(context.Background())
			if h := healthOf(t, s.Handler()); !h.Ok {
				t.Fatalf("next refresh: %+v", h)
			}
			if c := payloadOf(t, get(t, s.Handler(), "/api/status")).Cards[0]; *c.G != 42 {
				t.Fatalf("card after recovery = %+v", c)
			}
		})
	}
}

func TestRefreshCancelledIsNotLogged(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	src := &stubSource{refresh: func(context.Context) ([]sources.Event, error) {
		cancel()
		return nil, context.Canceled
	}}
	var buf bytes.Buffer
	s := New(config.Config{RefreshMs: 5000, ExtraCards: true}, Deps{
		Limits: &stubLimits{l: goodLimits()}, Mail: stubMail{}, Source: src,
		Now: func() time.Time { return fixedNow }, Log: quiet, ErrLog: log.New(&buf, "", 0),
	})
	s.RefreshOnce(ctx)
	if buf.Len() != 0 {
		t.Fatalf("logged on shutdown: %q", buf.String())
	}
	src.refresh = func(context.Context) ([]sources.Event, error) { return nil, errors.New("boom") }
	s.RefreshOnce(context.Background())
	if !strings.Contains(buf.String(), "refresh failed: boom") {
		t.Fatalf("real failure not logged: %q", buf.String())
	}
}

func TestJSONNotHTMLEscaped(t *testing.T) {
	ev := sources.Event{T: fixedNow.Add(-time.Hour).UnixMilli(), Model: "claude-opus-4", Project: "a&b<c>",
		Input: 1000, Output: 500, Cost: 1.25}
	src := &stubSource{events: []sources.Event{ev},
		refresh: func(context.Context) ([]sources.Event, error) { return nil, errors.New("x & <y>") }}
	s, _ := newTest(config.Config{RefreshMs: 5000, ExtraCards: true}, src, goodLimits())
	s.RefreshOnce(context.Background())
	for _, path := range []string{"/api/status", "/health"} {
		rec := get(t, s.Handler(), path)
		body := rec.Body.String()
		if strings.Contains(body, `\u00`) || !strings.Contains(body, "x & <y>") {
			t.Fatalf("%s escaped: %s", path, body)
		}
		if strings.HasSuffix(body, "\n") || rec.Header().Get("Content-Length") != strconv.Itoa(rec.Body.Len()) {
			t.Fatalf("%s framing: len %s body %d", path, rec.Header().Get("Content-Length"), rec.Body.Len())
		}
		if !json.Valid(rec.Body.Bytes()) {
			t.Fatalf("%s invalid JSON", path)
		}
	}
}
