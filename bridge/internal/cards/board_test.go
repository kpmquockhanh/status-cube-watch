package cards

import (
	"encoding/json"
	"strconv"
	"strings"
	"testing"
	"time"

	"claude-status-cube/bridge/internal/agents"
)

func TestBoardEmpty(t *testing.T) {
	b := BuildBoard(nil, agentsNow, nil)
	if b.S1 != "NO AGENTS" || b.C != "ink" || b.Ts != agentsNow.Unix() {
		t.Fatalf("board = %+v", b)
	}
	if j, _ := json.Marshal(b); !strings.Contains(string(j), `"agents":[]`) {
		t.Fatalf("agents should be [] not null: %s", j)
	}
}

func TestBoardEveryAgentAndPrompt(t *testing.T) {
	// More agents than the cube's MaxTiles, two of them waiting: the board
	// lists all of them and carries both prompts, whole.
	var list []agents.Agent
	for i := range MaxTiles + 3 {
		list = append(list, agent("r"+strconv.Itoa(i), "f"+strconv.Itoa(i), agents.Running, time.Minute))
	}
	long := strings.Repeat("x", PromptDetailChars*3)
	for i, since := range []time.Duration{time.Minute, time.Hour} {
		w := agent("w"+strconv.Itoa(i), "wait", agents.Waiting, since)
		w.Waiting = &agents.Prompt{ID: "p" + strconv.Itoa(i), Kind: "question", Title: "pick", Detail: long}
		for k := 1; k <= 8; k++ {
			w.Waiting.Options = append(w.Waiting.Options, agents.Option{Key: strconv.Itoa(k), Label: "opt"})
		}
		list = append(list, w)
	}
	b := BuildBoard(list, agentsNow, nil)
	if len(b.Agents) != len(list) || b.S1 != "2 NEED YOU" || b.C != "accent" {
		t.Fatalf("%d agents, s1 %q c %q", len(b.Agents), b.S1, b.C)
	}
	// Longest-waiting first, as on the cube.
	if b.Agents[0].I != "w1" || b.Agents[1].I != "w0" {
		t.Fatalf("order: %s, %s", b.Agents[0].I, b.Agents[1].I)
	}
	for _, a := range b.Agents[:2] {
		if a.Q == nil || a.Q.D != long || len(a.Q.O) != 8 || a.K != "wait" {
			t.Fatalf("prompt of %s = %+v", a.I, a.Q)
		}
	}
	for _, a := range b.Agents[2:] {
		if a.Q != nil || a.D == nil || a.D.I != a.I {
			t.Fatalf("%s: q %v d %v", a.I, a.Q, a.D)
		}
	}
	if b.Agents[0].D.HA != 1 || b.Agents[0].H == nil {
		t.Fatalf("automatic hat: ha %d h %v", b.Agents[0].D.HA, b.Agents[0].H)
	}
}

func TestBoardHatOverride(t *testing.T) {
	a := agent("a1", "f", agents.Idle, time.Hour)
	none := func(string) (int, bool) { return -1, true }
	b := BuildBoard([]agents.Agent{a}, agentsNow, none)
	if got := b.Agents[0]; got.H != nil || got.D.HA != 0 {
		t.Fatalf("override none: h %v ha %d", got.H, got.D.HA)
	}
}
