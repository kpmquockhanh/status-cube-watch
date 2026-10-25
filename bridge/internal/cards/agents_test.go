package cards

import (
	"encoding/json"
	"fmt"
	"reflect"
	"strings"
	"testing"
	"time"
	"unicode/utf8"

	"claude-status-cube/bridge/internal/agents"
	"claude-status-cube/bridge/internal/limits"
	"claude-status-cube/bridge/internal/mail"
	"claude-status-cube/bridge/internal/sources"
)

var agentsNow = time.Date(2026, 10, 24, 12, 0, 0, 0, time.UTC)

func agent(id, folder string, st agents.Status, since time.Duration) agents.Agent {
	a := agents.Agent{ID: id, Folder: folder, Status: st, StatusSince: agentsNow.Add(-since), Pane: "%1"}
	if st == agents.Waiting {
		a.Waiting = &agents.Prompt{ID: "p-" + id, Kind: "permission", Title: "run " + id + " secret title",
			Detail: id + " secret detail", Context: id + " secret context",
			Options: []agents.Option{{Key: "1", Label: id + " allow label", Primary: true}, {Key: "3", Label: id + " deny label"}}}
	}
	return a
}

func fourAgents() []agents.Agent {
	w2 := agent("a1", "short-wait", agents.Waiting, 2*time.Minute)
	w5 := agent("a2", "long-wait", agents.Waiting, 5*time.Minute)
	run := agent("a3", "runner", agents.Running, time.Minute)
	run.Activity = &agents.Activity{Tool: "Edit", Detail: "/Users/me/secret/path.go"}
	run.Subagents = make([]agents.Subagent, 3)
	run.LastPrompt = "my secret last prompt"
	run.Context.Used, run.Context.Window = 50_000, 200_000
	idle := agent("a4", "sleepy", agents.Idle, 12*time.Minute)
	idle.Unseen = true
	idle.LastPrompt = "another secret prompt"
	return []agents.Agent{w2, run, idle, w5}
}

func agentsCard(t *testing.T, p Payload) *Card {
	t.Helper()
	for i := range p.Cards {
		if p.Cards[i].T == "AGENTS" {
			return &p.Cards[i]
		}
	}
	return nil
}

func TestAgentsCardOrderAndSummary(t *testing.T) {
	p := Build(nil, Opts{Agents: fourAgents(), ShowAgents: true, Full: true}, agentsNow)
	if len(p.Cards) != 2 || p.Cards[0].T != "CLAUDE" || p.Cards[1].T != "AGENTS" {
		t.Fatalf("cards = %+v", p.Cards)
	}
	c := p.Cards[1]
	if c.V != "4" || c.S1 != "2 NEED YOU" || c.S2 != "" || c.C != "accent" {
		t.Errorf("card = v %q s1 %q s2 %q c %q", c.V, c.S1, c.S2, c.C)
	}
	var ids, lines, poses, kinds []string
	for _, tl := range c.A {
		ids, lines, poses, kinds = append(ids, tl.I), append(lines, tl.L), append(poses, tl.P), append(kinds, tl.K)
	}
	want := func(name string, got, w []string) {
		if !reflect.DeepEqual(got, w) {
			t.Errorf("%s = %q, want %q", name, got, w)
		}
	}
	want("ids", ids, []string{"a2", "a1", "a3", "a4"})
	want("lines", lines, []string{"allow?", "allow?", "edit · 25% ctx", "hungry · feed me"})
	want("poses", poses, []string{"wait", "wait", "write", "hungry"})
	want("kinds", kinds, []string{"wait", "wait", "run", "idle"})
	if c.A[2].X != 25 || c.A[3].U != 1 || c.A[2].U != 0 || c.A[0].N != "long-wait" {
		t.Errorf("tiles = %+v", c.A)
	}
	q := c.Q
	if q == nil || q.I != "a2" || q.P != "p-a2" || q.K != "permission" || q.X != "" ||
		q.T != "run a2 secret title" || q.D != "a2 secret detail" || q.C != "a2 secret context" ||
		!reflect.DeepEqual(q.O, []PromptOption{{K: "1", L: "a2 allow label"}, {K: "3", L: "a2 deny label"}}) {
		t.Errorf("q = %+v", q)
	}
	b, err := Marshal(p)
	if err != nil {
		t.Fatal(err)
	}
	for _, key := range []string{`"a":[`, `"q":{`, `"o":[{"k":"1","l":"a2 allow label"}`} {
		if !strings.Contains(string(b), key) {
			t.Errorf("payload lacks %s: %s", key, b)
		}
	}
}

func TestAgentsSummaryWords(t *testing.T) {
	cases := []struct {
		st       []agents.Status
		s1, c, v string
	}{
		{[]agents.Status{agents.Waiting, agents.Running}, "1 NEEDS YOU", "accent", "2"},
		{[]agents.Status{agents.Running, agents.Running, agents.Running, agents.Idle}, "3 RUNNING", "blue", "4"},
		{[]agents.Status{agents.Idle, agents.Exited, agents.Starting, agents.Error}, "ALL IDLE", "ink", "4"},
	}
	for _, tc := range cases {
		var list []agents.Agent
		for i, st := range tc.st {
			list = append(list, agent(fmt.Sprint("x", i), "f", st, time.Minute))
		}
		c := agentsCard(t, Build(nil, Opts{Agents: list, ShowAgents: true}, agentsNow))
		if c == nil || c.S1 != tc.s1 || c.C != tc.c || c.V != tc.v {
			t.Errorf("%v: card = %+v", tc.st, c)
		}
	}
}

func TestAgentsTileOrderAndPoses(t *testing.T) {
	mk := func(id, folder string, st agents.Status, tool string) agents.Agent {
		a := agent(id, folder, st, 30*time.Second)
		if tool != "" {
			a.Activity = &agents.Activity{Tool: tool}
		}
		return a
	}
	list := []agents.Agent{
		mk("e1", "zz", agents.Exited, ""),
		mk("i1", "bb", agents.Idle, ""),
		mk("r2", "b", agents.Running, "Bash"),
		mk("x1", "aa", agents.Error, ""),
		mk("s1", "aa", agents.Starting, ""),
		mk("r1", "a", agents.Running, ""),
		mk("w1", "q", agents.Waiting, ""),
	}
	c := agentsCard(t, Build(nil, Opts{Agents: list, ShowAgents: true}, agentsNow))
	var got []string
	for _, tl := range c.A {
		got = append(got, tl.I+":"+tl.K+":"+tl.P+":"+tl.L)
	}
	want := []string{"w1:wait:wait:allow?", "r1:run:think:thinking", "r2:run:bash:bash",
		"s1:start:start:starting...", "x1:err:err:error", "i1:idle:idle:done · 30s ago", "e1:exit:exit:exited"}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("tiles = %q\nwant  %q", got, want)
	}

	poses := map[string]string{"Read": "read", "Grep": "read", "Glob": "read", "LS": "read",
		"Edit": "write", "MultiEdit": "write", "Write": "write", "NotebookEdit": "write", "Bash": "bash",
		"WebFetch": "web", "WebSearch": "web", "TodoWrite": "plan", "ExitPlanMode": "plan",
		"Task": "delegate", "Agent": "delegate", "Compact": "compact", "mcp__x__y": "run"}
	for tool, pose := range poses {
		if got := tileOf(mk("r", "f", agents.Running, tool), agentsNow, nil).P; got != pose {
			t.Errorf("pose(%s) = %q, want %q", tool, got, pose)
		}
	}
}

func TestAgentsIdleAgeAndContext(t *testing.T) {
	for since, want := range map[time.Duration]string{
		59 * time.Second: "done · 59s ago", 12*time.Minute + 59*time.Second: "done · 12m ago",
		3*time.Hour + 59*time.Minute: "done · 3h ago", 50 * time.Hour: "done · 2d ago",
	} {
		if got := tileOf(agent("i", "f", agents.Idle, since), agentsNow, nil).L; got != want {
			t.Errorf("idle %v = %q, want %q", since, got, want)
		}
	}
	for _, tc := range []struct {
		used, window int64
		want         int
	}{{1, 200, 1}, {3, 1000, 0}, {5, 1000, 1}, {300, 200, 100}, {-5, 100, 0}, {10, 0, 0}} {
		a := agent("c", "f", agents.Idle, 0)
		a.Context.Used, a.Context.Window = tc.used, tc.window
		if got := tileOf(a, agentsNow, nil).X; got != tc.want {
			t.Errorf("context %d/%d = %d, want %d", tc.used, tc.window, got, tc.want)
		}
	}
}

func TestAgentsWaitTieBreak(t *testing.T) {
	a := agent("b2", "same", agents.Waiting, time.Minute)
	b := agent("b1", "same", agents.Waiting, time.Minute)
	c := agent("c0", "alpha", agents.Waiting, time.Minute)
	card := agentsCard(t, Build(nil, Opts{Agents: []agents.Agent{a, b, c}, ShowAgents: true, Full: true}, agentsNow))
	var got []string
	for _, tl := range card.A {
		got = append(got, tl.I)
	}
	if !reflect.DeepEqual(got, []string{"c0", "b1", "b2"}) || card.Q.I != "c0" {
		t.Errorf("order = %v, q = %+v", got, card.Q)
	}
}

func TestLANVariantHasNoPromptText(t *testing.T) {
	list := fourAgents()
	p := Build(nil, Opts{Agents: list, ShowAgents: true, Full: false}, agentsNow)
	c := agentsCard(t, p)
	if c == nil || c.Q != nil || len(c.A) != 4 {
		t.Fatalf("card = %+v", c)
	}
	b, err := Marshal(p)
	if err != nil {
		t.Fatal(err)
	}
	var secrets []string
	for _, a := range list {
		secrets = append(secrets, a.LastPrompt)
		if a.Activity != nil {
			secrets = append(secrets, a.Activity.Detail)
		}
		if w := a.Waiting; w != nil {
			secrets = append(secrets, w.Title, w.Detail, w.Context)
			for _, o := range w.Options {
				secrets = append(secrets, o.Label)
			}
		}
	}
	for _, s := range secrets {
		if s != "" && strings.Contains(string(b), s) {
			t.Errorf("LAN payload carries %q: %s", s, b)
		}
	}
	if strings.Contains(string(b), "secret") {
		t.Errorf("LAN payload carries a secret: %s", b)
	}
}

func TestNotInTmux(t *testing.T) {
	a := agent("t1", "f", agents.Waiting, time.Minute)
	a.Pane = ""
	c := agentsCard(t, Build(nil, Opts{Agents: []agents.Agent{a}, ShowAgents: true, Full: true}, agentsNow))
	if c == nil || c.Q == nil || c.Q.X != "tmux" {
		t.Fatalf("card = %+v", c)
	}
}

func TestNoAgentsNoCard(t *testing.T) {
	base := Opts{Limits: &limits.Limits{Error: "timeout"}, ExtraCards: true}
	plain := Build(nil, base, agentsNow)

	on := base
	on.ShowAgents, on.Full = true, true
	if p := Build(nil, on, agentsNow); !reflect.DeepEqual(p, plain) {
		t.Errorf("no agents: %+v\nwant %+v", p, plain)
	}
	off := base
	off.Agents, off.Full = fourAgents(), true
	p := Build(nil, off, agentsNow)
	if !reflect.DeepEqual(p, plain) {
		t.Errorf("agents off: %+v\nwant %+v", p, plain)
	}
	got, _ := json.Marshal(p)
	want, _ := json.Marshal(plain)
	if string(got) != string(want) {
		t.Errorf("agents off marshals differently:\n%s\n%s", got, want)
	}
}

func TestAgentsCardPosition(t *testing.T) {
	p := Build(nil, Opts{Error: "boom", ExtraCards: true, Agents: fourAgents(), ShowAgents: true}, agentsNow)
	var titles []string
	for _, c := range p.Cards {
		titles = append(titles, c.T)
	}
	want := []string{"BRIDGE ERROR", "CLAUDE", "AGENTS", "TODAY", "THIS MONTH", "TOKENS TODAY", "TOP MODEL", "TOP PROJECT"}
	if !reflect.DeepEqual(titles, want) {
		t.Errorf("titles = %v", titles)
	}
}

// budgetOpts: the worst case the brief names. Eight agents with long
// non-ASCII folder names, all waiting; the oldest asks a question with six
// 200-character options and a 3000-character detail.
func budgetOpts(extra bool) Opts {
	var list []agents.Agent
	for i := range 8 {
		folder := strings.Repeat("dự-án-đẹp-", 6) // 60 characters, accents and đ
		a := agent(fmt.Sprintf("l%05d", i), folder, agents.Waiting, time.Duration(10-i)*time.Minute)
		a.Context.Used, a.Context.Window = 199_999, 200_000
		a.Unseen = true
		if i == 0 {
			p := &agents.Prompt{ID: "p1234567", Kind: "question", Title: strings.Repeat("Tiêu đề ", 20),
				Detail: strings.Repeat("chi tiết ", 334), Context: strings.Repeat("ngữ cảnh ", 20)}
			for k := range 6 {
				p.Options = append(p.Options, agents.Option{Key: fmt.Sprint(k + 1), Label: strings.Repeat("é", 200)})
			}
			a.Waiting = p
		}
		list = append(list, a)
	}
	unread := 123456
	five, seven := 99.0, 99.0
	return Opts{
		Error: strings.Repeat("x", 100),
		Limits: &limits.Limits{FiveHour: &limits.Window{Utilization: &five, ResetsAt: "2026-10-24T15:00:00Z"},
			SevenDay: &limits.Window{Utilization: &seven, ResetsAt: "2026-10-30T15:00:00Z"}, Error: strings.Repeat("y", 100)},
		Mail:       &mail.Mail{Unread: &unread},
		ExtraCards: extra, Agents: list, ShowAgents: true, Full: true,
	}
}

func budgetEvents() []sources.Event {
	return []sources.Event{{T: agentsNow.UnixMilli() - 1000, Model: "claude-opus-5-5-20261001",
		Project: strings.Repeat("project-name-", 5), Input: 123_456_789, Output: 9_876_543, Cost: 12345.67}}
}

func size(t *testing.T, p Payload) int {
	t.Helper()
	b, err := Marshal(p)
	if err != nil {
		t.Fatal(err)
	}
	return len(b)
}

func TestAgentsPayloadBudget(t *testing.T) {
	for _, extra := range []bool{true, false} {
		for _, full := range []bool{true, false} {
			o := budgetOpts(extra)
			o.Full = full
			if n := size(t, Build(budgetEvents(), o, agentsNow)); n > MaxPayloadBytes {
				t.Errorf("extra %v full %v: %d bytes", extra, full, n)
			}
		}
	}
	p := Build(budgetEvents(), budgetOpts(false), agentsNow)
	c := agentsCard(t, p)
	if len(c.A) != MaxTiles || c.Q == nil {
		t.Fatalf("without extra cards: %d tiles, q %+v", len(c.A), c.Q)
	}
	if c.A[0].N != "du-an-dep-du-..." || c.Q.T == "" || c.Q.D == "" || len(c.Q.O) != 6 {
		t.Errorf("tile %+v q %+v", c.A[0], c.Q)
	}
	// With the extra cards on, they go before any prompt data.
	p = Build(budgetEvents(), budgetOpts(true), agentsNow)
	c = agentsCard(t, p)
	if len(c.A) != MaxTiles || c.Q == nil || c.Q.D == "" || c.Q.Cut != 0 || len(c.Q.O) != 6 {
		t.Fatalf("with extra cards: %d tiles, q %+v", len(c.A), c.Q)
	}
	if all := build(budgetEvents(), budgetOpts(true), agentsNow, 1<<20); len(p.Cards) >= len(all.Cards) {
		t.Errorf("no extra card was dropped: %d cards", len(p.Cards))
	}
}

// Fix 4: a cut title or detail ends in "..." within its maximum; short ones
// are left alone; dropped detail and prompt are marked.
func TestPromptCutMarks(t *testing.T) {
	a := agent("m1", "f", agents.Waiting, time.Minute)
	a.Waiting.Title = strings.Repeat("title ", 20)
	a.Waiting.Detail = strings.Repeat("detail ", 40)
	q := agentsCard(t, Build(nil, Opts{Agents: []agents.Agent{a}, ShowAgents: true, Full: true}, agentsNow)).Q
	if len(q.T) != PromptTitleChars || !strings.HasSuffix(q.T, "...") || !strings.HasPrefix(q.T, "title title") {
		t.Errorf("title %q (%d)", q.T, len(q.T))
	}
	if len(q.D) > PromptDetailChars || !strings.HasSuffix(q.D, "...") || strings.HasSuffix(q.D, " ...") {
		t.Errorf("detail %q (%d)", q.D, len(q.D))
	}
	if q.Cut != 0 {
		t.Errorf("cut set without the budget: %+v", q)
	}
	b := agent("m2", "f", agents.Waiting, time.Minute)
	b.Waiting.Title = strings.Repeat("x", PromptTitleChars)
	if q := agentsCard(t, Build(nil, Opts{Agents: []agents.Agent{b}, ShowAgents: true, Full: true}, agentsNow)).Q; q.T != b.Waiting.Title {
		t.Errorf("a title that fits was changed: %q", q.T)
	}

	o := budgetOpts(false)
	events := budgetEvents()
	full := build(events, o, agentsNow, 1<<20)
	noDetail := full
	noDetail.Cards = append([]Card(nil), full.Cards...)
	q2 := *noDetail.Cards[2].Q
	q2.D = ""
	noDetail.Cards[2].Q = &q2
	got := build(events, o, agentsNow, size(t, noDetail)+len(`,"cut":1`))
	if c := agentsCard(t, got); c.Q == nil || c.Q.D != "" || c.Q.Cut != 1 || c.QCut != 0 {
		t.Fatalf("detail dropped: %+v", c.Q)
	}
	b2, _ := Marshal(got)
	if !strings.Contains(string(b2), `"d":""`) || !strings.Contains(string(b2), `"cut":1`) {
		t.Errorf("cut not in the JSON: %s", b2)
	}
	// The largest budget that drops the prompt marks it.
	budget := MaxPayloadBytes
	for ; budget > 0; budget-- {
		if c := agentsCard(t, build(events, o, agentsNow, budget)); c == nil || c.Q == nil {
			break
		}
	}
	small := build(events, o, agentsNow, budget)
	c := agentsCard(t, small)
	if c == nil || c.Q != nil || c.QCut != 1 {
		t.Fatalf("prompt dropped at %d: %+v", budget, c)
	}
	if b3, _ := Marshal(small); !strings.Contains(string(b3), `"qc":1`) {
		t.Errorf("qc not in the JSON: %s", b3)
	}
	// The LAN variant never had a prompt: no mark.
	o.Full = false
	if c := agentsCard(t, build(events, o, agentsNow, budget)); c == nil || c.QCut != 0 {
		t.Fatalf("LAN: %+v", c)
	}
}

func TestAgentsTileCap(t *testing.T) {
	var list []agents.Agent
	for i := range 11 {
		list = append(list, agent(fmt.Sprint("r", i), fmt.Sprintf("f%02d", i), agents.Running, time.Minute))
	}
	c := agentsCard(t, Build(nil, Opts{Agents: list, ShowAgents: true}, agentsNow))
	if c.V != "11" || len(c.A) != MaxTiles || c.A[7].I != "r7" || c.S1 != "11 RUNNING" {
		t.Errorf("card = %+v", c)
	}
}

// TestAgentsTrimOrder runs the trimming steps one by one: with the budget set
// to exactly the size each step leaves, that step is the last one taken.
func TestAgentsTrimOrder(t *testing.T) {
	o := budgetOpts(true)
	events := budgetEvents()
	full := build(events, o, agentsNow, 1<<20)
	agentsAt := 2 // BRIDGE ERROR, CLAUDE, AGENTS, extras...

	clone := func(p Payload) Payload {
		var c Payload
		b, _ := json.Marshal(p)
		_ = json.Unmarshal(b, &c)
		return c
	}
	if len(full.Cards[agentsAt].Q.O) != 6 || full.Cards[agentsAt].Q.D == "" || len(full.Cards[agentsAt].A) != 8 {
		t.Fatalf("untrimmed = %+v", full.Cards[agentsAt])
	}

	var expect []Payload
	step := clone(full)
	ac := func() *Card { return &step.Cards[agentsAt] }
	for len(step.Cards) > agentsAt+1 {
		step.Cards = step.Cards[:len(step.Cards)-1]
		expect = append(expect, clone(step)) // 1: extra cards from the end
	}
	ac().Q.D, ac().Q.Cut = "", 1
	expect = append(expect, clone(step)) // 2: detail, marked
	ac().Q.O = ac().Q.O[:4]
	expect = append(expect, clone(step)) // 3: options beyond 4
	for len(ac().A) > 4 {
		ac().A = ac().A[:len(ac().A)-1]
		expect = append(expect, clone(step)) // 4: tiles from the end
	}
	ac().Q, ac().QCut = nil, 1
	expect = append(expect, clone(step)) // 5: prompt, marked
	step.Cards = step.Cards[:agentsAt]
	expect = append(expect, clone(step)) // last resort: the Agents card

	prev := size(t, full)
	for i, want := range expect {
		n := size(t, want)
		if n >= prev {
			t.Fatalf("step %d does not shrink: %d >= %d", i, n, prev)
		}
		prev = n
		got := clone(build(events, o, agentsNow, n))
		if !reflect.DeepEqual(got, want) {
			gb, _ := Marshal(got)
			wb, _ := Marshal(want)
			t.Errorf("budget %d (step %d):\n got  %s\n want %s", n, i, gb, wb)
		}
		if got := clone(build(events, o, agentsNow, n+1)); !reflect.DeepEqual(got, want) {
			t.Errorf("budget %d+1 (step %d) went further than needed", n, i)
		}
	}
}

func TestMarshalNoHTMLEscapes(t *testing.T) {
	b, err := Marshal(Payload{V: 1, Src: "<a&b>", Cards: []Card{}})
	if err != nil {
		t.Fatal(err)
	}
	if string(b) != `{"v":1,"ts":0,"src":"<a&b>","cards":[]}` {
		t.Errorf("Marshal = %s", b)
	}
}

func TestTileMetaWording(t *testing.T) {
	line := func(a agents.Agent) string { return tileOf(a, agentsNow, nil).L }
	run := agent("r", "f", agents.Running, time.Minute)
	if got := line(run); got != "thinking" {
		t.Errorf("running, no tool = %q", got)
	}
	run.Activity = &agents.Activity{Tool: "Bash"}
	if got := line(run); got != "bash" {
		t.Errorf("running no context = %q", got)
	}
	run.Context.Used, run.Context.Window = 41, 100
	if got := line(run); got != "bash · 41% ctx" {
		t.Errorf("running = %q", got)
	}
	for tool, verb := range map[string]string{"Task": "agent", "TodoWrite": "plan", "WebSearch": "search",
		"WebFetch": "web", "MultiEdit": "edit", "Read": "read"} {
		run.Activity.Tool = tool
		if got := line(run); got != verb+" · 41% ctx" {
			t.Errorf("verb(%s) = %q", tool, got)
		}
	}
	run.Activity.Tool = "Compact"
	if got := line(run); got != "compacting · 41%" {
		t.Errorf("compact = %q", got)
	}
	run.Activity.Tool = "mcp__server__averylongtoolname"
	if got := line(run); utf8.RuneCountInString(got) > TileLineChars || len(got) > TileLineBytes || !strings.HasSuffix(got, "...") {
		t.Errorf("long verb = %q", got)
	}

	w := agent("w", "f", agents.Waiting, time.Minute)
	w.Waiting.Context = "bash \u00b7 ~/code/x"
	if got := line(w); got != "allow bash?" {
		t.Errorf("permission = %q", got)
	}
	w.Waiting.Context = "~/code/secret-project"
	if got := line(w); got != "allow?" {
		t.Errorf("permission, path context = %q", got)
	}
	w.Waiting.Kind = "question"
	w.Waiting.Options = make([]agents.Option, 3)
	if got := line(w); got != "pick 1 of 3" {
		t.Errorf("question = %q", got)
	}
	w.Waiting.Kind = "input"
	if got := line(w); got != "needs input" {
		t.Errorf("input = %q", got)
	}
	un := agent("u", "f", agents.Idle, time.Minute)
	un.Unseen = true
	if got := line(un); got != "hungry · feed me" {
		t.Errorf("unseen = %q", got)
	}
	if got := line(agent("i", "f", agents.Idle, 5*time.Minute)); got != "done · 5m ago" {
		t.Errorf("idle = %q", got)
	}
	e := agent("e", "f", agents.Error, time.Minute)
	if got := line(e); got != "error" {
		t.Errorf("error = %q", got)
	}
	e.ErrorText = "api 529 overloaded and then some"
	if got := line(e); got != "api 529 overl..." {
		t.Errorf("error text = %q", got)
	}
	if got := line(agent("s", "f", agents.Starting, 0)); got != "starting..." {
		t.Errorf("starting = %q", got)
	}
	if got := line(agent("x", "f", agents.Exited, 0)); got != "exited" {
		t.Errorf("exited = %q", got)
	}
}

func TestCelebratePose(t *testing.T) {
	pose := func(since time.Duration, unseen bool) string {
		a := agent("c", "f", agents.Idle, since)
		a.Unseen = unseen
		return tileOf(a, agentsNow, nil).P
	}
	if got := pose(3*time.Second, false); got != "celebrate" {
		t.Errorf("3 s = %q", got)
	}
	if got := pose(7*time.Second, false); got != "idle" {
		t.Errorf("7 s = %q", got)
	}
	if got := pose(3*time.Second, true); got != "hungry" {
		t.Errorf("unseen = %q", got)
	}
	if got := pose(5*time.Minute, true); got != "hungry" {
		t.Errorf("unseen old = %q", got)
	}
}

// A tile's automatic hat comes from its session, not its folder, so it does
// not change when the session's folder does, and every agent wears one.
func TestTileHat(t *testing.T) {
	a := agent("h", "docs-site", agents.Idle, time.Hour)
	a.SessionID = "33333333-cccc-4ccc-8ccc-cccccccccccc"
	s, c := HatOf(HatKey(a))
	for _, cwd := range []string{"/Users/me/code/docs-site", ""} {
		a.Cwd = cwd
		if h := tileOf(a, agentsNow, nil).H; h == nil || *h != s*8+c {
			t.Errorf("cwd %q: hat %v, want %d", cwd, h, s*8+c)
		}
	}
	a.Folder = ""
	if h := tileOf(a, agentsNow, nil).H; h == nil || *h != s*8+c {
		t.Errorf("no folder: hat %v, want %d", h, s*8+c)
	}
}

func TestToneSteps(t *testing.T) {
	for pct, want := range map[int]string{0: "ink", 59: "ink", 60: "amber", 84: "amber", 85: "accent", 100: "accent"} {
		if got := tone(pct); got != want {
			t.Errorf("tone(%d) = %q, want %q", pct, got, want)
		}
	}
}

func TestHatsWithinBudget(t *testing.T) {
	for _, full := range []bool{true, false} {
		o := budgetOpts(true)
		o.Full = full
		for i := range o.Agents {
			o.Agents[i].Cwd = fmt.Sprintf("/Users/someone/code/project-%d", i)
		}
		p := Build(budgetEvents(), o, agentsNow)
		if n := size(t, p); n > MaxPayloadBytes {
			t.Errorf("full %v: %d bytes", full, n)
		}
		c := agentsCard(t, p)
		if c == nil || len(c.A) < 4 || (full && c.Q == nil) {
			t.Fatalf("full %v: card %+v", full, c)
		}
		for _, tl := range c.A {
			if tl.H == nil {
				t.Errorf("tile %s has no hat", tl.I)
			}
		}
	}
}

// A tile's subagent badge is agents-deck's: a mini critter, plus "×N" for
// more than one.
func TestTileSubagentBadge(t *testing.T) {
	a := agent("r", "f", agents.Running, time.Minute)
	if s := tileOf(a, agentsNow, nil).S; s != nil {
		t.Errorf("no subagents = %q, want absent", *s)
	}
	a.Subagents = make([]agents.Subagent, 1)
	if s := tileOf(a, agentsNow, nil).S; s == nil || *s != "" {
		t.Errorf("one subagent = %v, want \"\"", s)
	}
	a.Subagents = make([]agents.Subagent, 3)
	if s := tileOf(a, agentsNow, nil).S; s == nil || *s != "×3" {
		t.Errorf("three subagents = %v, want ×3", s)
	}
	b, _ := json.Marshal(tileOf(agent("r", "f", agents.Running, time.Minute), agentsNow, nil))
	if strings.Contains(string(b), `"s"`) {
		t.Errorf("no subagents marshals %s", b)
	}
}

// Only the separator is non-ASCII: tool names and error text are folded.
func TestTileLineFoldsToASCII(t *testing.T) {
	e := agent("e", "f", agents.Error, time.Minute)
	e.ErrorText = "café “x”"
	if got := tileOf(e, agentsNow, nil).L; got != `cafe "x"` {
		t.Errorf("error = %q", got)
	}
	r := agent("r", "f", agents.Running, time.Minute)
	r.Activity = &agents.Activity{Tool: "Édit☃"}
	if got := tileOf(r, agentsNow, nil).L; got != "edit" {
		t.Errorf("verb = %q", got)
	}
}

// A tile is named by its session's title, as agents-deck's board is, so two
// agents in one folder read apart; with no title it falls back to the folder.
func TestTileNameIsTheTitle(t *testing.T) {
	a := agent("r", "claude-status-cube", agents.Running, time.Minute)
	if got := tileOf(a, agentsNow, nil).N; got != "claude-status..." {
		t.Errorf("no title = %q", got)
	}
	a.Title = "Agents deck tiles"
	if got := tileOf(a, agentsNow, nil).N; got != "Agents deck t..." {
		t.Errorf("long title = %q", got)
	}
	a.Title = "fix login"
	if got := tileOf(a, agentsNow, nil).N; got != "fix login" {
		t.Errorf("title = %q", got)
	}
}
