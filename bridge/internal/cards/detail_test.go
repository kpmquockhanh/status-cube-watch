package cards

import (
	"strings"
	"testing"
	"time"
	"unicode/utf8"

	"claude-status-cube/bridge/internal/agents"
	"claude-status-cube/bridge/internal/limits"
)

func detailAgent() agents.Agent {
	a := agent("a3", "x", agents.Running, 38*time.Minute)
	a.Title = "fix login"
	a.Activity = &agents.Activity{Tool: "Edit", Detail: "/Users/me/x/src/a.ts"}
	a.Context.Used, a.Context.Window = 410_000, 1_000_000
	a.ModelLabel = "opus 5.5"
	a.Cwd = "/Users/me/x"
	a.Branch = "main"
	a.LastPrompt = "please fix the login redirect"
	a.Subagents = make([]agents.Subagent, 3)
	return a
}

func buildFull(list []agents.Agent) Payload {
	return Build(nil, Opts{Source: "t", Limits: &limits.Limits{}, Agents: list, ShowAgents: true, Full: true}, agentsNow)
}

func TestDetailFields(t *testing.T) {
	t.Setenv("HOME", "/Users/me")
	a := detailAgent()
	p := WithDetail(buildFull([]agents.Agent{a}), a, true, agentsNow)
	d := agentsCard(t, p).D
	if d == nil {
		t.Fatal("no d")
	}
	if d.I != "a3" || d.T != "fix login" || d.S != "running"+sep+"38m" || d.C != "41%"+sep+"410k / 1m" ||
		d.M != "opus 5.5"+sep+"~/x"+sep+"main" || d.N != "3 subagents" || d.HA != 1 || d.P != a.LastPrompt || d.E != "" {
		t.Fatalf("d = %+v", d)
	}
	if !strings.HasPrefix(d.W, "edit"+sep) || !strings.Contains(d.W, "src/a.ts") {
		t.Fatalf("w = %q", d.W)
	}
	if p2 := WithDetail(buildFull([]agents.Agent{a}), a, false, agentsNow); agentsCard(t, p2).D.HA != 0 {
		t.Fatal("ha should be 0 with an override")
	}
}

func TestDetailWords(t *testing.T) {
	a := agent("a1", "f", agents.Idle, time.Hour)
	a.Unseen = true
	d := agentsCard(t, WithDetail(buildFull([]agents.Agent{a}), a, true, agentsNow)).D
	if d.S != "idle"+sep+"1h" || d.W != "idle" || d.T != "f" || d.C != "" || d.M != "f" || d.N != "" {
		t.Fatalf("idle: %+v", d)
	}
	e := agent("a2", "f", agents.Error, time.Minute)
	e.ErrorText = "boom"
	d = agentsCard(t, WithDetail(buildFull([]agents.Agent{e}), e, true, agentsNow)).D
	if d.S != "error"+sep+"1m" || d.E != "boom" {
		t.Fatalf("error: %+v", d)
	}
	w := agent("a5", "f", agents.Waiting, time.Minute)
	if d = agentsCard(t, WithDetail(buildFull([]agents.Agent{w}), w, true, agentsNow)).D; !strings.HasPrefix(d.S, "needs you") {
		t.Fatalf("waiting: %+v", d)
	}
	one := detailAgent()
	one.Subagents = one.Subagents[:1]
	if d = agentsCard(t, WithDetail(buildFull([]agents.Agent{one}), one, true, agentsNow)).D; d.N != "1 subagent" {
		t.Fatalf("n = %q", d.N)
	}
}

func TestDetailCount(t *testing.T) {
	for n, want := range map[int64]string{0: "0", 999: "999", 41_000: "41k", 410_000: "410k", 1_200_000: "1.2m", 1_000_000: "1m", 1_500: "1.5k", 999_900: "1m"} {
		if got := detailCount(n); got != want {
			t.Errorf("detailCount(%d) = %q, want %q", n, got, want)
		}
	}
}

func TestDetailByteLimits(t *testing.T) {
	a := detailAgent()
	a.Title = strings.Repeat("été ", 30) // folds to ASCII
	a.Activity.Detail = strings.Repeat("segment/", 30)
	a.LastPrompt = strings.Repeat("x", 500)
	a.Branch = strings.Repeat("branch-", 20)
	a.ErrorText = strings.Repeat("e", 200)
	d := agentsCard(t, WithDetail(buildFull([]agents.Agent{a}), a, true, agentsNow)).D
	for name, f := range map[string]struct {
		s   string
		max int
	}{"t": {d.T, DetailTitleChars}, "s": {d.S, DetailStatusChars}, "w": {d.W, DetailDoingChars}, "c": {d.C, DetailCtxChars},
		"p": {d.P, DetailPromptChars}, "m": {d.M, DetailMetaChars}, "n": {d.N, DetailSubsChars}} {
		if len(f.s) > f.max || !utf8.ValidString(f.s) {
			t.Errorf("%s = %q (%d bytes, max %d)", name, f.s, len(f.s), f.max)
		}
	}
	for _, s := range []string{d.T, d.W, d.M, d.P} {
		if len(s) > 0 && !strings.HasSuffix(s, "...") {
			t.Errorf("%q not marked as cut", s)
		}
	}
	if !strings.HasPrefix(d.W, "edit"+sep) {
		t.Errorf("w = %q", d.W)
	}
	ae := a
	ae.Status = agents.Error
	if e := agentsCard(t, WithDetail(buildFull([]agents.Agent{ae}), ae, true, agentsNow)).D.E; len(e) > DetailErrChars || !strings.HasSuffix(e, "...") {
		t.Errorf("e = %q", e)
	}
}

func TestDetailBudget(t *testing.T) {
	// The worst Agents card (8 tiles, a 6-option prompt with full detail) plus
	// the longest d fits by itself; pad the payload with an extra card to
	// squeeze d through every trim step, in order.
	var list []agents.Agent
	for i := 0; i < 7; i++ {
		x := agent("b"+string(rune('0'+i)), "folder", agents.Error, time.Minute)
		x.Title, x.ErrorText = strings.Repeat("N", 30), strings.Repeat("e", 40)
		x.Subagents = make([]agents.Subagent, 2)
		list = append(list, x)
	}
	w := agent("w1", "waiter", agents.Waiting, time.Hour)
	w.Waiting.Title = strings.Repeat("t", 80)
	w.Waiting.Detail = strings.Repeat("d", 300)
	w.Waiting.Context = strings.Repeat("c", 80)
	for i := 0; i < 6; i++ {
		w.Waiting.Options = append(w.Waiting.Options, agents.Option{Key: string(rune('1' + i)), Label: strings.Repeat("o", 60)})
	}
	w.LastPrompt = strings.Repeat("p ", 1000)
	w.Cwd, w.Title, w.Branch, w.ModelLabel = "/Users/me/"+strings.Repeat("w", 40), strings.Repeat("T", 60), strings.Repeat("b", 60), strings.Repeat("m", 60)
	w.Subagents = make([]agents.Subagent, 12)
	list = append(list, w)

	seen := map[string]bool{}
	for pad := 0; pad < 700; pad++ {
		base := buildFull(list)
		base.Cards = append(base.Cards, Card{T: "PAD", S1: strings.Repeat("x", pad)})
		if !fits(base, MaxPayloadBytes) {
			break
		}
		p := WithDetail(base, w, true, agentsNow)
		b, err := Marshal(p)
		if err != nil || len(b) > MaxPayloadBytes {
			t.Fatalf("pad %d: %d bytes, err %v", pad, len(b), err)
		}
		c := agentsCard(t, p)
		if c.Q == nil || len(c.A) != 8 {
			t.Fatalf("pad %d: d cost the full variant something (q %v, %d tiles)", pad, c.Q != nil, len(c.A))
		}
		switch {
		case c.D == nil:
			seen["dropped"] = true
		case c.D.P == "":
			seen["noprompt"] = true
		case len(c.D.P) <= detailPromptShort:
			seen["cut"] = true
		default:
			seen["full"] = true
		}
		if base.Cards[len(base.Cards)-1].S1 != strings.Repeat("x", pad) || agentsCard(t, base).D != nil {
			t.Fatal("WithDetail modified its input")
		}
	}
	for _, k := range []string{"full", "cut", "noprompt", "dropped"} {
		if !seen[k] {
			t.Errorf("trim step %q never reached", k)
		}
	}
}

func TestDetailNoAgentsCard(t *testing.T) {
	a := detailAgent()
	p := buildFull(nil)
	if got := WithDetail(p, a, true, agentsNow); len(got.Cards) != len(p.Cards) {
		t.Fatal("no Agents card: payload must be unchanged")
	}
}
