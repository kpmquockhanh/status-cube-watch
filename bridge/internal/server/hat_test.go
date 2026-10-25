package server

import (
	"encoding/json"
	"path/filepath"
	"testing"
	"time"

	"claude-status-cube/bridge/internal/agents"
	"claude-status-cube/bridge/internal/cards"
	"claude-status-cube/bridge/internal/config"
	"claude-status-cube/bridge/internal/hatstore"
)

func tileHat(t *testing.T, s *Server) *int {
	t.Helper()
	rec := serve(s, cubeReq("GET", "/api/status", ""))
	var p cards.Payload
	if err := json.Unmarshal(rec.Body.Bytes(), &p); err != nil {
		t.Fatal(err)
	}
	for _, c := range p.Cards {
		if c.T == "AGENTS" && len(c.A) > 0 {
			return c.A[0].H
		}
	}
	t.Fatal("no agents card")
	return nil
}

func TestHatEndpoint(t *testing.T) {
	ag := newStubAgents(agents.Agent{ID: "a1", Folder: "proj", Cwd: "/x/proj", Status: agents.Idle,
		StatusSince: fixedNow.Add(-time.Hour)})
	d := Deps{Limits: &stubLimits{l: goodLimits()}, Mail: stubMail{}, Source: &stubSource{},
		Now: func() time.Time { return fixedNow }, Log: quiet, ErrLog: quiet, Agents: ag,
		Hats: hatstore.Open(filepath.Join(t.TempDir(), "hats.json"), nil)}
	s := New(config.Config{RefreshMs: 5000, Port: 8787, Agents: true}, d)

	if rec := serve(s, cubeReq("POST", "/api/hat", `{"agent":"a1","hat":17}`)); rec.Code != 204 {
		t.Fatalf("set: %d %s", rec.Code, rec.Body)
	}
	if h := tileHat(t, s); h == nil || *h != 17 {
		t.Fatalf("tile hat after set = %v", h)
	}
	if rec := serve(s, cubeReq("POST", "/api/hat", `{"agent":"a1","hat":-1}`)); rec.Code != 204 {
		t.Fatalf("none: %d", rec.Code)
	}
	if h := tileHat(t, s); h != nil {
		t.Fatalf("tile hat after none = %d", *h)
	}
	if rec := serve(s, cubeReq("POST", "/api/hat", `{"agent":"a1","hat":null}`)); rec.Code != 204 {
		t.Fatalf("auto: %d", rec.Code)
	}
	sh, col := cards.HatOf("/x/proj")
	if h := tileHat(t, s); h == nil || *h != sh*8+col {
		t.Fatalf("tile hat after auto = %v", h)
	}

	for name, tc := range map[string]struct {
		body string
		code int
	}{
		"unknown agent": {`{"agent":"zz","hat":3}`, 404},
		"out of range":  {`{"agent":"a1","hat":104}`, 400},
		"below range":   {`{"agent":"a1","hat":-2}`, 400},
		"string hat":    {`{"agent":"a1","hat":"x"}`, 400},
		"missing hat":   {`{"agent":"a1"}`, 400},
		"missing agent": {`{"hat":3}`, 400},
		"not json":      {`nope`, 400},
	} {
		if rec := serve(s, cubeReq("POST", "/api/hat", tc.body)); rec.Code != tc.code {
			t.Errorf("%s: %d, want %d", name, rec.Code, tc.code)
		}
	}
	if rec := serve(s, req("POST", "/api/hat", "127.0.0.1:5000", "127.0.0.1:8787", `{"agent":"a1","hat":3}`)); rec.Code != 404 {
		t.Errorf("no client header: %d", rec.Code)
	}
	if rec := serve(s, cubeReq("GET", "/api/hat", "")); rec.Code != 405 {
		t.Errorf("GET: %d", rec.Code)
	}
	if rec := serve(newAgentsTest(nil, nil), cubeReq("POST", "/api/hat", `{"agent":"a1","hat":3}`)); rec.Code != 404 {
		t.Errorf("agents off: %d", rec.Code)
	}
}
