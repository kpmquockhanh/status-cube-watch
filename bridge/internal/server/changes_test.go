package server

import (
	"path/filepath"
	"testing"
	"time"

	"claude-status-cube/bridge/internal/agents"
	"claude-status-cube/bridge/internal/config"
	"claude-status-cube/bridge/internal/hatstore"
)

func changesServer(t *testing.T) *Server {
	t.Helper()
	ag := newStubAgents(agents.Agent{ID: "a1", SessionID: "11111111-aaaa-4aaa-8aaa-aaaaaaaaaaaa", Folder: "proj",
		Status: agents.Idle, StatusSince: fixedNow.Add(-time.Hour)})
	d := Deps{Limits: &stubLimits{l: goodLimits()}, Mail: stubMail{}, Source: &stubSource{},
		Now: func() time.Time { return fixedNow }, Log: quiet, ErrLog: quiet, Agents: ag,
		Hats: hatstore.Open(filepath.Join(t.TempDir(), "hats.json"), nil)}
	return New(config.Config{RefreshMs: 5000, Port: 8787, Agents: true}, d)
}

// A waiting /api/changes answers 204 as soon as the agents change, so the
// Mac app fetches at once instead of at its next poll.
func TestChangesAnswersOnChange(t *testing.T) {
	s := changesServer(t)
	s.changesWait = 5 * time.Second
	got := make(chan int, 1)
	go func() { got <- serve(s, cubeReq("GET", "/api/changes", "")).Code }()
	time.Sleep(50 * time.Millisecond) // the request is waiting
	s.notifyChanged()
	select {
	case code := <-got:
		if code != 204 {
			t.Fatalf("after a change: %d, want 204", code)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("still waiting after a change")
	}
}

// Nothing changed within the wait: 304, and the Mac asks again.
func TestChangesTimesOut(t *testing.T) {
	s := changesServer(t)
	s.changesWait = 30 * time.Millisecond
	if code := serve(s, cubeReq("GET", "/api/changes", "")).Code; code != 304 {
		t.Fatalf("no change: %d, want 304", code)
	}
}

// A hat set through /api/hat is a change too (the store is not the tracker).
func TestChangesOnHat(t *testing.T) {
	s := changesServer(t)
	s.changesWait = 5 * time.Second
	got := make(chan int, 1)
	go func() { got <- serve(s, cubeReq("GET", "/api/changes", "")).Code }()
	time.Sleep(50 * time.Millisecond)
	if rec := serve(s, cubeReq("POST", "/api/hat", `{"agent":"a1","hat":17}`)); rec.Code != 204 {
		t.Fatalf("hat: %d", rec.Code)
	}
	select {
	case code := <-got:
		if code != 204 {
			t.Fatalf("after a hat: %d, want 204", code)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("still waiting after a hat")
	}
}

func TestChangesGuard(t *testing.T) {
	s := changesServer(t)
	s.changesWait = 10 * time.Millisecond
	if code := serve(s, req("GET", "/api/changes", "127.0.0.1:5000", "127.0.0.1:8787", "")).Code; code != 404 {
		t.Errorf("no client header: %d, want 404", code)
	}
	if code := serve(s, req("GET", "/api/changes", "192.168.1.9:5000", "192.168.1.2:8787", "")).Code; code != 404 {
		t.Errorf("LAN: %d, want 404", code)
	}
	if code := serve(s, cubeReq("POST", "/api/changes", "")).Code; code != 405 {
		t.Errorf("POST: %d, want 405", code)
	}
}
