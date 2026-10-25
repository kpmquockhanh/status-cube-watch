package cards

import (
	"testing"
	"time"

	"claude-status-cube/bridge/internal/agents"
)

// Hats are per session: two sessions in one folder wear their own automatic
// hats, and an override changes only the session it was set on.
func TestTileHatPerSession(t *testing.T) {
	a, b := agent("a", "proj", agents.Idle, time.Hour), agent("b", "proj", agents.Idle, time.Hour)
	a.Cwd, b.Cwd = "/x/proj", "/x/proj"
	a.SessionID, b.SessionID = "11111111-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "22222222-bbbb-4bbb-8bbb-bbbbbbbbbbbb"
	if HatKey(a) == HatKey(b) {
		t.Fatalf("one key for two sessions: %q", HatKey(a))
	}
	hat := func(tl Tile) int {
		if tl.H == nil {
			return -1
		}
		return *tl.H
	}
	auto := func(x agents.Agent) int { s, c := HatOf(HatKey(x)); return s*hatColors + c }
	if auto(a) == auto(b) {
		t.Fatalf("test sessions hash to one hat (%d): pick other ids", auto(a))
	}

	c := agentsCardOf([]agents.Agent{a, b}, false, agentsNow, nil)
	if hat(c.A[0]) != auto(a) || hat(c.A[1]) != auto(b) {
		t.Errorf("automatic hats = %d, %d, want %d, %d", hat(c.A[0]), hat(c.A[1]), auto(a), auto(b))
	}

	for _, tc := range []struct {
		name string
		code int // the override on a's session
	}{{"override", 17}, {"none", -1}} {
		hats := func(k string) (int, bool) { return tc.code, k == HatKey(a) }
		c := agentsCardOf([]agents.Agent{a, b}, false, agentsNow, hats)
		if hat(c.A[0]) != tc.code {
			t.Errorf("%s: a's hat = %d, want %d", tc.name, hat(c.A[0]), tc.code)
		}
		if hat(c.A[1]) != auto(b) {
			t.Errorf("%s: b's hat = %d, want its own %d", tc.name, hat(c.A[1]), auto(b))
		}
	}

	// Without a session id (a hook-less session), the agent id is the key.
	n := agent("c", "proj", agents.Idle, time.Hour)
	if HatKey(n) == "" || HatKey(n) == HatKey(a) {
		t.Errorf("key without a session id = %q", HatKey(n))
	}
}
