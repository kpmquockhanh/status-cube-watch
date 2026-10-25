package server

import (
	"context"
	"net/http"
	"strings"
	"testing"
	"time"

	"claude-status-cube/bridge/internal/agents"
	"claude-status-cube/bridge/internal/cards"
)

func agentsOf(t *testing.T, p cards.Payload) cards.Card {
	t.Helper()
	for _, c := range p.Cards {
		if c.T == "AGENTS" {
			return c
		}
	}
	t.Fatal("no Agents card")
	return cards.Card{}
}

func detailRun() agents.Agent {
	return agents.Agent{ID: "a9", Folder: "proj", Cwd: "/x/proj", Status: agents.Running, StatusSince: fixedNow.Add(-time.Minute),
		LastPrompt: "my secret last prompt", Activity: &agents.Activity{Tool: "Edit", Detail: "/x/proj/secret.go"}}
}

func TestStatusDetail(t *testing.T) {
	s := newAgentsTest(newStubAgents(waitingAgent(), detailRun()), nil)
	s.RefreshOnce(context.Background())

	loop := serve(s, req("GET", "/api/status?detail=a9", "127.0.0.1:5000", "127.0.0.1:8787", ""))
	c := agentsOf(t, payloadOf(t, loop))
	if c.D == nil || c.D.I != "a9" || c.D.P != "my secret last prompt" {
		t.Fatalf("loopback detail: %s", loop.Body)
	}
	if c.Q == nil {
		t.Fatal("detail payload lost q")
	}
	for _, path := range []string{"/api/status?detail=nope", "/api/status?detail=", "/api/status"} {
		rec := serve(s, req("GET", path, "127.0.0.1:5000", "127.0.0.1:8787", ""))
		if strings.Contains(rec.Body.String(), `"d":{`) {
			t.Fatalf("%s carries d: %s", path, rec.Body)
		}
	}
}

func TestDetailNeverLAN(t *testing.T) {
	s := newAgentsTest(newStubAgents(detailRun()), nil)
	s.RefreshOnce(context.Background())
	for _, r := range []*http.Request{
		req("GET", "/api/status?detail=a9", "192.168.1.20:5000", "192.168.1.5:8787", ""),
		req("GET", "/api/status?detail=a9", "127.0.0.1:5000", "x:8787", ""),
	} {
		rec := serve(s, r)
		if strings.Contains(rec.Body.String(), `"d":{`) || strings.Contains(rec.Body.String(), "secret") {
			t.Fatalf("LAN detail leaks: %s", rec.Body)
		}
	}
}
