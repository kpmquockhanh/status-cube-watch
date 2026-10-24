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
	"net/http/httptest"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"claude-status-cube/bridge/internal/agents"
	"claude-status-cube/bridge/internal/cards"
	"claude-status-cube/bridge/internal/config"
	"claude-status-cube/bridge/internal/mail"
)

type answerCall struct{ agent, prompt, key string }

type stubAgents struct {
	mu        sync.Mutex
	list      []agents.Agent
	hooks     [][]byte
	hookErr   error
	answers   []answerCall
	answerErr error
	syncs     atomic.Int32
	tick      chan struct{}
}

func newStubAgents(list ...agents.Agent) *stubAgents {
	return &stubAgents{list: list, tick: make(chan struct{}, 1)}
}

func (a *stubAgents) Hook(b []byte) error {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.hooks = append(a.hooks, append([]byte(nil), b...))
	return a.hookErr
}
func (a *stubAgents) Sync(context.Context) { a.syncs.Add(1) }
func (a *stubAgents) Answer(_ context.Context, agent, prompt, key string) error {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.answers = append(a.answers, answerCall{agent, prompt, key})
	return a.answerErr
}
func (a *stubAgents) Snapshot() []agents.Agent {
	a.mu.Lock()
	defer a.mu.Unlock()
	return append([]agents.Agent(nil), a.list...)
}
func (a *stubAgents) Subscribe() (<-chan struct{}, func()) { return a.tick, func() {} }

// set replaces the agents and ticks, like a tracker change.
func (a *stubAgents) set(list ...agents.Agent) {
	a.mu.Lock()
	a.list = list
	a.mu.Unlock()
	select {
	case a.tick <- struct{}{}:
	default:
	}
}

func (a *stubAgents) setErrs(hook, answer error) {
	a.mu.Lock()
	a.hookErr, a.answerErr = hook, answer
	a.mu.Unlock()
}

func (a *stubAgents) counts() (hooks, answers int) {
	a.mu.Lock()
	defer a.mu.Unlock()
	return len(a.hooks), len(a.answers)
}

const secretTitle = "Run rm -rf /tmp/secret-thing?"

func waitingAgent() agents.Agent {
	return agents.Agent{ID: "a1", Folder: "cube", Status: agents.Waiting, StatusSince: fixedNow.Add(-time.Minute),
		Pane: "%3",
		Waiting: &agents.Prompt{ID: "p1", Kind: "permission", Title: secretTitle, Detail: "rm -rf /tmp/secret-thing",
			Options: []agents.Option{{Key: "1", Label: "Yes"}, {Key: "2", Label: "No"}}}}
}

// newAgentsTest builds a server on port 8787; ag == nil = agents off.
func newAgentsTest(ag *stubAgents, errLog *log.Logger) *Server {
	if errLog == nil {
		errLog = quiet
	}
	d := Deps{Limits: &stubLimits{l: goodLimits()}, Mail: stubMail{}, Source: &stubSource{},
		Now: func() time.Time { return fixedNow }, Log: quiet, ErrLog: errLog}
	if ag != nil {
		d.Agents = ag
	}
	return New(config.Config{RefreshMs: 5000, Port: 8787, Agents: ag != nil}, d)
}

// req builds a request as if it arrived on port 8787 from remote.
func req(method, path, remote, host, body string) *http.Request {
	r := httptest.NewRequest(method, path, strings.NewReader(body))
	r.RemoteAddr = remote
	r.Host = host
	return r.WithContext(context.WithValue(r.Context(), http.LocalAddrContextKey,
		&net.TCPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 8787}))
}

// cubeReq is a request from the bridge's own hook command or the Mac helper.
func cubeReq(method, path, body string) *http.Request {
	r := req(method, path, "127.0.0.1:5000", "127.0.0.1:8787", body)
	r.Header.Set("X-Cube-Client", "1")
	return r
}

func serve(s *Server, r *http.Request) *httptest.ResponseRecorder {
	rec := httptest.NewRecorder()
	s.Handler().ServeHTTP(rec, r)
	return rec
}

func TestLoopbackGuard(t *testing.T) {
	for _, tc := range []struct {
		name, remote, host, origin string
		want                       bool
	}{
		{"127 host", "127.0.0.1:5000", "127.0.0.1:8787", "", true},
		{"localhost host", "127.0.0.1:5000", "localhost:8787", "", true},
		{"dns rebinding", "127.0.0.1:5000", "evil.test:8787", "", false},
		{"lan remote", "192.168.1.20:5000", "127.0.0.1:8787", "", false},
		{"origin", "127.0.0.1:5000", "127.0.0.1:8787", "http://127.0.0.1:8787", false},
		{"other port", "127.0.0.1:5000", "127.0.0.1:9999", "", false},
		{"no port", "127.0.0.1:5000", "127.0.0.1", "", false},
		{"ipv6 loopback", "[::1]:5000", "localhost:8787", "", true},
		{"ipv4-mapped loopback", "[::ffff:127.0.0.1]:5000", "127.0.0.1:8787", "", true},
		{"ipv4-mapped lan", "[::ffff:192.168.1.20]:5000", "127.0.0.1:8787", "", false},
		{"garbage remote", "nonsense", "127.0.0.1:8787", "", false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			r := httptest.NewRequest("GET", "/api/status", nil)
			r.RemoteAddr, r.Host = tc.remote, tc.host
			if tc.origin != "" {
				r.Header.Set("Origin", tc.origin)
			}
			if got := loopbackRequest(r, 8787); got != tc.want {
				t.Fatalf("loopbackRequest = %v, want %v", got, tc.want)
			}
		})
	}
	// An Origin header that is present but empty still marks a browser.
	r := httptest.NewRequest("GET", "/", nil)
	r.RemoteAddr, r.Host = "127.0.0.1:5000", "127.0.0.1:8787"
	r.Header["Origin"] = []string{""}
	if loopbackRequest(r, 8787) {
		t.Fatal("empty Origin header passed the guard")
	}
}

func TestStatusVariants(t *testing.T) {
	s := newAgentsTest(newStubAgents(waitingAgent()), nil)
	s.RefreshOnce(context.Background())

	full := serve(s, req("GET", "/api/status", "127.0.0.1:5000", "127.0.0.1:8787", ""))
	p := payloadOf(t, full)
	if len(p.Cards) != 2 || p.Cards[1].T != "AGENTS" || p.Cards[1].Q == nil || p.Cards[1].Q.T != secretTitle {
		t.Fatalf("loopback payload: %s", full.Body)
	}
	if full.Header().Get("Content-Length") != strconv.Itoa(full.Body.Len()) {
		t.Fatalf("framing: %v", full.Header())
	}
	if want, _ := cards.Marshal(p); !bytes.Equal(want, full.Body.Bytes()) {
		t.Fatalf("not served as cards.Marshal:\n%s\n%s", full.Body, want)
	}

	for _, r := range []*http.Request{
		req("GET", "/api/status", "192.168.1.20:5000", "192.168.1.5:8787", ""),
		req("GET", "/api/status", "127.0.0.1:5000", "x:8787", ""),
	} {
		lan := serve(s, r)
		p := payloadOf(t, lan)
		if len(p.Cards) != 2 || p.Cards[1].T != "AGENTS" || len(p.Cards[1].A) != 1 || p.Cards[1].Q != nil {
			t.Fatalf("LAN payload: %s", lan.Body)
		}
		for _, secret := range []string{"secret", `"q"`, "Yes"} {
			if strings.Contains(lan.Body.String(), secret) {
				t.Fatalf("LAN payload leaks %q: %s", secret, lan.Body)
			}
		}
	}
}

func TestHookEndpoint(t *testing.T) {
	body := `{"hook_event_name":"Stop","session_id":"x"}`
	ag := newStubAgents()
	s := newAgentsTest(ag, nil)

	rec := serve(s, cubeReq("POST", "/hook", body))
	if rec.Code != 204 || rec.Header().Get("Content-Length") != "0" {
		t.Fatalf("hook: %d %v", rec.Code, rec.Header())
	}
	if rec.Header().Get("Access-Control-Allow-Origin") != "" {
		t.Fatal("CORS header on /hook")
	}
	ag.mu.Lock()
	got := string(ag.hooks[0])
	ag.mu.Unlock()
	if got != body {
		t.Fatalf("Hook got %q", got)
	}

	// An empty body and a failing tracker are still 204.
	ag.setErrs(errors.New("bad hook"), nil)
	if rec := serve(s, cubeReq("POST", "/hook", "")); rec.Code != 204 {
		t.Fatalf("failing hook: %d", rec.Code)
	}

	noHeader := req("POST", "/hook", "127.0.0.1:5000", "127.0.0.1:8787", body)
	lan := req("POST", "/hook", "192.168.1.20:5000", "127.0.0.1:8787", body)
	lan.Header.Set("X-Cube-Client", "1")
	browser := cubeReq("POST", "/hook", body)
	browser.Header.Set("Origin", "http://evil.test")
	for name, r := range map[string]*http.Request{"no header": noHeader, "lan": lan, "browser": browser} {
		if rec := serve(s, r); rec.Code != 404 {
			t.Fatalf("%s: %d", name, rec.Code)
		}
	}
	if n, _ := ag.counts(); n != 2 {
		t.Fatalf("Hook called %d times, want 2", n)
	}

	if rec := serve(newAgentsTest(nil, nil), cubeReq("POST", "/hook", body)); rec.Code != 404 {
		t.Fatalf("agents off: %d", rec.Code)
	}
}

func TestHookErrorsLoggedOnce(t *testing.T) {
	ag := newStubAgents()
	ag.setErrs(errors.New("bad hook"), nil)
	var buf bytes.Buffer
	s := newAgentsTest(ag, log.New(&buf, "", 0))
	for range 3 {
		serve(s, cubeReq("POST", "/hook", "{}"))
	}
	if n := strings.Count(buf.String(), "bad hook"); n != 1 {
		t.Fatalf("logged %d times: %q", n, buf.String())
	}
	for i := range 300 {
		ag.setErrs(errors.New("err "+strconv.Itoa(i)), nil)
		serve(s, cubeReq("POST", "/hook", "{}"))
	}
	if n := strings.Count(buf.String(), "\n"); n > maxHookErrors+2 {
		t.Fatalf("logged %d lines for 300 distinct errors", n)
	}
}

func answerBody(agent, prompt, key string) string {
	b, _ := json.Marshal(map[string]string{"agent": agent, "prompt": prompt, "key": key})
	return string(b)
}

func TestAnswerEndpoint(t *testing.T) {
	ag := newStubAgents(waitingAgent())
	s := newAgentsTest(ag, nil)
	for _, tc := range []struct {
		err  error
		code int
		body string
	}{
		{nil, 204, ""},
		{agents.ErrStale, 409, `{"error":"that prompt is no longer open"}`},
		{agents.ErrBadKey, 400, `{"error":"not one of the prompt's options"}`},
		{agents.ErrNotFound, 404, `{"error":"no such agent"}`},
		{agents.ErrNotInTmux, 409, `{"error":"not in tmux"}`},
		{fmt.Errorf("wrapped: %w", agents.ErrNotInTmux), 409, `{"error":"not in tmux"}`},
		{errors.New("tmux died"), 500, `{"error":"tmux died"}`},
	} {
		ag.setErrs(nil, tc.err)
		rec := serve(s, cubeReq("POST", "/api/answer", answerBody("a1", "p1", "1")))
		if rec.Code != tc.code || rec.Body.String() != tc.body ||
			rec.Header().Get("Content-Length") != strconv.Itoa(rec.Body.Len()) {
			t.Fatalf("%v: %d %q %v", tc.err, rec.Code, rec.Body, rec.Header())
		}
		if rec.Header().Get("Access-Control-Allow-Origin") != "" {
			t.Fatal("CORS header on /api/answer")
		}
	}
	ag.mu.Lock()
	last := ag.answers[len(ag.answers)-1]
	ag.mu.Unlock()
	if last != (answerCall{"a1", "p1", "1"}) {
		t.Fatalf("Answer got %+v", last)
	}

	ag.setErrs(nil, nil)
	_, before := ag.counts()
	for name, body := range map[string]string{
		"not json":      "nope",
		"empty body":    "",
		"missing key":   `{"agent":"a1","prompt":"p1"}`,
		"empty agent":   answerBody("", "p1", "1"),
		"number key":    `{"agent":"a1","prompt":"p1","key":1}`,
		"too large":     `{"agent":"a1","prompt":"p1","key":"1","pad":"` + strings.Repeat("x", 5000) + `"}`,
		"trailing data": answerBody("a1", "p1", "1") + "{}",
	} {
		if rec := serve(s, cubeReq("POST", "/api/answer", body)); rec.Code != 400 {
			t.Fatalf("%s: %d", name, rec.Code)
		}
	}
	if rec := serve(s, cubeReq("GET", "/api/answer", "")); rec.Code != 405 {
		t.Fatalf("GET: %d", rec.Code)
	}
	if _, after := ag.counts(); after != before {
		t.Fatalf("Answer called for bad requests")
	}
}

func TestAnswerEndpointStale(t *testing.T) {
	ag := newStubAgents(waitingAgent())
	s := newAgentsTest(ag, nil)
	browser := cubeReq("POST", "/api/answer", answerBody("a1", "p1", "1"))
	browser.Header.Set("Origin", "http://127.0.0.1:8787")
	noHeader := req("POST", "/api/answer", "127.0.0.1:5000", "127.0.0.1:8787", answerBody("a1", "p1", "1"))
	rebind := cubeReq("POST", "/api/answer", answerBody("a1", "p1", "1"))
	rebind.Host = "evil.test:8787"
	lan := cubeReq("POST", "/api/answer", answerBody("a1", "p1", "1"))
	lan.RemoteAddr = "192.168.1.20:5000"
	for name, r := range map[string]*http.Request{"browser": browser, "no header": noHeader, "rebind": rebind, "lan": lan} {
		if rec := serve(s, r); rec.Code != 404 {
			t.Fatalf("%s: %d", name, rec.Code)
		}
	}
	if _, n := ag.counts(); n != 0 {
		t.Fatalf("Answer called %d times", n)
	}

	ag.setErrs(nil, agents.ErrStale)
	if rec := serve(s, cubeReq("POST", "/api/answer", answerBody("a1", "p0", "1"))); rec.Code != 409 {
		t.Fatalf("stale: %d", rec.Code)
	}

	if rec := serve(newAgentsTest(nil, nil), cubeReq("POST", "/api/answer", answerBody("a1", "p1", "1"))); rec.Code != 404 {
		t.Fatalf("agents off: %d", rec.Code)
	}
}

type countingMail struct{ calls atomic.Int32 }

func (m *countingMail) Get(context.Context, string, string) *mail.Mail { m.calls.Add(1); return nil }

func TestPromptPushedQuickly(t *testing.T) {
	ag := newStubAgents()
	lim := &stubLimits{l: goodLimits()}
	m := &countingMail{}
	s := New(config.Config{RefreshMs: 5000, Port: 8787, Agents: true}, Deps{Limits: lim, Mail: m,
		Source: &stubSource{}, Agents: ag, Now: func() time.Time { return fixedNow }, Log: quiet, ErrLog: quiet})
	s.RefreshOnce(context.Background())
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go s.watchAgents(ctx)

	start := time.Now()
	ag.set(waitingAgent())
	for {
		p := payloadOf(t, serve(s, req("GET", "/api/status", "127.0.0.1:5000", "127.0.0.1:8787", "")))
		if len(p.Cards) == 2 && p.Cards[1].Q != nil && p.Cards[1].Q.T == secretTitle {
			if *p.Cards[0].G != 42 {
				t.Fatalf("usage card lost its limits: %+v", p.Cards[0])
			}
			break
		}
		if time.Since(start) > 400*time.Millisecond {
			t.Fatalf("prompt not pushed within 400 ms")
		}
		time.Sleep(10 * time.Millisecond)
	}
	if lim.calls.Load() != 1 || m.calls.Load() != 1 {
		t.Fatalf("rebuild re-read limits %d / mail %d times", lim.calls.Load(), m.calls.Load())
	}
	lan := serve(s, req("GET", "/api/status", "192.168.1.20:5000", "127.0.0.1:8787", ""))
	if strings.Contains(lan.Body.String(), "secret") || !strings.Contains(lan.Body.String(), "AGENTS") {
		t.Fatalf("LAN after rebuild: %s", lan.Body)
	}
}

func TestAgentsOffUnchanged(t *testing.T) {
	s := newAgentsTest(nil, nil)
	// Plan 1's output for these stubs, captured before the agents work.
	const starting = `{"v":1,"ts":1792843200,"src":"stub","cards":[{"t":"CLAUDE","v":"--","s1":"starting","s2":"starting up","c":"ink","g":-1,"c2":"ink","g2":-1,"rows":[{"k":"5H","p":"--","r":""},{"k":"7D","p":"--","r":""}]}]}`
	const after = `{"v":1,"ts":1792843200,"src":"stub","cards":[{"t":"CLAUDE","v":"2h 00m","s1":"to reset","s2":"","c":"green","g":42,"c2":"green","g2":10,"rows":[{"k":"5H","p":"42%","r":"2h 00m"},{"k":"7D","p":"10%","r":"3d 12h"}]}]}`
	check := func(want string) {
		t.Helper()
		for _, r := range []*http.Request{
			req("GET", "/api/status", "127.0.0.1:5000", "127.0.0.1:8787", ""),
			req("GET", "/api/status", "192.168.1.20:5000", "192.168.1.5:8787", ""),
		} {
			if got := serve(s, r).Body.String(); got != want {
				t.Fatalf("got  %s\nwant %s", got, want)
			}
		}
	}
	check(starting)
	s.RefreshOnce(context.Background())
	check(after)
}

func TestSyncLoopStops(t *testing.T) {
	ag := newStubAgents()
	s := newAgentsTest(ag, nil)
	s.syncEvery = 10 * time.Millisecond
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() { s.syncLoop(ctx); close(done) }()
	time.Sleep(100 * time.Millisecond)
	cancel()
	<-done
	n := ag.syncs.Load()
	if n < 3 {
		t.Fatalf("Sync called %d times in 100 ms", n)
	}
	time.Sleep(50 * time.Millisecond)
	if ag.syncs.Load() != n {
		t.Fatal("Sync called after cancel")
	}
}

// Over a real listener on a random port, the guard compares Host with the
// port the request arrived on.
func TestRealListenerGuard(t *testing.T) {
	ag := newStubAgents(waitingAgent())
	s := newAgentsTest(ag, nil)
	s.RefreshOnce(context.Background())
	ts := httptest.NewServer(s.Handler())
	defer ts.Close()
	port := ts.Listener.Addr().(*net.TCPAddr).Port

	resp, err := http.Get(ts.URL + "/api/status")
	if err != nil {
		t.Fatal(err)
	}
	b, _ := io.ReadAll(resp.Body)
	resp.Body.Close()
	if !strings.Contains(string(b), secretTitle) {
		t.Fatalf("127.0.0.1:%d did not get the full payload: %s", port, b)
	}

	r, _ := http.NewRequest("POST", ts.URL+"/hook", strings.NewReader("{}"))
	r.Header.Set("X-Cube-Client", "1")
	r.Host = "localhost:" + strconv.Itoa(port)
	resp, err = http.DefaultClient.Do(r)
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	if resp.StatusCode != 204 || resp.ContentLength != 0 {
		t.Fatalf("hook over a real listener: %d len %d", resp.StatusCode, resp.ContentLength)
	}
	if n, _ := ag.counts(); n != 1 {
		t.Fatalf("Hook called %d times", n)
	}
}
