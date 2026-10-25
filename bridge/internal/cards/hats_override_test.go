package cards

import (
	"testing"
	"time"

	"claude-status-cube/bridge/internal/agents"
)

func TestTileHatOverride(t *testing.T) {
	a, b := agent("a", "proj", agents.Idle, time.Hour), agent("b", "proj", agents.Idle, time.Hour)
	a.Cwd, b.Cwd = "/x/proj", "/x/proj"
	for _, tc := range []struct {
		name string
		hats func(string) (int, bool)
		want int // -2 = automatic, -1 = no hat
	}{
		{"override", func(f string) (int, bool) { return 17, f == "/x/proj" }, 17},
		{"none", func(string) (int, bool) { return -1, true }, -1},
		{"auto", func(string) (int, bool) { return 17, false }, -2},
	} {
		c := agentsCardOf([]agents.Agent{a, b}, false, agentsNow, tc.hats)
		for _, tl := range c.A {
			switch {
			case tc.want == -1 && tl.H != nil:
				t.Errorf("%s: hat %d", tc.name, *tl.H)
			case tc.want >= 0 && (tl.H == nil || *tl.H != tc.want):
				t.Errorf("%s: hat %v", tc.name, tl.H)
			case tc.want == -2:
				s, col := HatOf("/x/proj")
				if tl.H == nil || *tl.H != s*8+col {
					t.Errorf("%s: hat %v", tc.name, tl.H)
				}
			}
		}
	}
}
