package agents

// Tests for the whole-branch review fixes (numbers are the fix brief's).

import (
	"context"
	"errors"
	"fmt"
	"path/filepath"
	"slices"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"
)

// pid is the tracker's n-th prompt id.
func pid(tr *Tracker, n int) string { return tr.promptPrefix + strconv.Itoa(n) }

func permissionHook(t *testing.T, tr *Tracker, cmd string) {
	t.Helper()
	mustHook(t, tr, map[string]any{"hook_event_name": "PermissionRequest", "tool_name": "Bash",
		"tool_input": map[string]string{"command": cmd}})
}

// Fix 1: a send that may have typed (timeout, kill, cancel) keeps the claim:
// no second tap types into the pane until the prompt changes.
func TestAnswerAmbiguousSendKeepsClaim(t *testing.T) {
	for _, sendErr := range []error{context.DeadlineExceeded, context.Canceled, errors.New("signal: killed")} {
		tr, env := newTestTracker(t)
		discovered(t, tr, env, "")
		id := agentID(sidA)
		permissionHook(t, tr, "make")
		env.run.setErr("tmux send-keys -t %3 -l 1", sendErr)
		if err := tr.Answer(context.Background(), id, pid(tr, 1), "1"); err == nil || errors.Is(err, ErrStale) {
			t.Fatalf("%v: first answer %v", sendErr, err)
		}
		env.run.setErr("tmux send-keys -t %3 -l 1", nil)
		if err := tr.Answer(context.Background(), id, pid(tr, 1), "1"); !errors.Is(err, ErrStale) {
			t.Fatalf("%v: a retry must not type again: %v", sendErr, err)
		}
		if got := env.run.matching("tmux send-keys"); len(got) != 1 {
			t.Fatalf("%v: sent %q", sendErr, got)
		}
		// A new prompt can be answered.
		permissionHook(t, tr, "make again")
		if err := tr.Answer(context.Background(), id, pid(tr, 2), "1"); err != nil {
			t.Fatalf("%v: next prompt: %v", sendErr, err)
		}
	}
}

// Fix 1: tmux refusing before it sent anything gives the prompt back.
func TestAnswerRefusedSendReleasesClaim(t *testing.T) {
	for _, msg := range []string{"can't find pane: %3", "no server running on /private/tmp/tmux-501/default"} {
		tr, env := newTestTracker(t)
		discovered(t, tr, env, "")
		id := agentID(sidA)
		permissionHook(t, tr, "make")
		env.run.setErr("tmux send-keys -t %3 -l 1", errors.New(msg))
		if err := tr.Answer(context.Background(), id, pid(tr, 1), "1"); err == nil || errors.Is(err, ErrStale) {
			t.Fatalf("%s: %v", msg, err)
		}
		env.run.setErr("tmux send-keys -t %3 -l 1", nil)
		if err := tr.Answer(context.Background(), id, pid(tr, 1), "1"); err != nil {
			t.Fatalf("%s: retry: %v", msg, err)
		}
	}
}

// Fix 3: a multi-select question is answered at the terminal.
func TestApplyMultiSelectIsInput(t *testing.T) {
	e := &entry{}
	in := `{"questions":[{"question":"Which ones?","multiSelect":true,"options":[{"label":"x"},{"label":"y"}]}]}`
	apply(e, ev("PreToolUse", "tool", "AskUserQuestion", "input", in), hookNow)
	if e.Waiting == nil || e.Waiting.Kind != "input" || len(e.Waiting.Options) != 0 {
		t.Fatalf("multi-select should be input-only: %+v", e.Waiting)
	}
	in = `{"questions":[{"question":"Which one?","multiSelect":false,"options":[{"label":"x"},{"label":"y"}]}]}`
	apply(e, ev("PreToolUse", "tool", "AskUserQuestion", "input", in), hookNow)
	if e.Waiting == nil || e.Waiting.Kind != "question" || len(e.Waiting.Options) != 2 {
		t.Fatalf("single-select stays a question: %+v", e.Waiting)
	}
}

// Fix 5: any hook proves Claude is up.
func TestFirstHookIsNeverStarting(t *testing.T) {
	tr, _ := newTestTracker(t)
	mustHook(t, tr, map[string]any{"hook_event_name": "Notification", "notification_type": "idle_prompt"})
	if a := only(t, tr); a.Status != Idle {
		t.Fatalf("after a first idle_prompt: %s", a.Status)
	}
}

// Fix 5: a /model-style "Enter to confirm" menu on an idle agent that sends
// hooks is not a prompt (it is the user's own dialog, and hooks own the status).
func TestMenuPaneOnHookSeenIdleAgent(t *testing.T) {
	tr, env := newTestTracker(t)
	mustHook(t, tr, map[string]any{"hook_event_name": "Notification", "notification_type": "idle_prompt"})
	env.run.set("tmux capture-pane", trustPane)
	discovered(t, tr, env, "") // no transcript yet
	tr.Sync(context.Background())
	if a := only(t, tr); a.Status != Idle || a.Waiting != nil {
		t.Fatalf("menu became a prompt: %+v %+v", a, a.Waiting)
	}
}

// Fix 5: a hook prompt that arrives while a menu prompt is open replaces it,
// and the next Sync (menu gone from the pane) does not wipe it.
func TestHookPromptSurvivesMenuSync(t *testing.T) {
	tr, env := newTestTracker(t)
	env.run.set("tmux capture-pane", trustPane)
	discovered(t, tr, env, "")
	if a := only(t, tr); a.Status != Waiting || a.Waiting.Kind != "permission" || a.Waiting.Title != "trust this folder?" {
		t.Fatalf("menu prompt: %+v", a.Waiting)
	}
	env.run.set("tmux capture-pane", permissionPane)
	permissionHook(t, tr, "make")
	tr.Sync(context.Background())
	a := only(t, tr)
	if a.Status != Waiting || a.Waiting == nil || a.Waiting.Title != "run this command?" {
		t.Fatalf("hook prompt wiped: %+v %+v", a, a.Waiting)
	}
}

// Fix 5: after a menu answer, an agent that sends hooks is idle, not starting.
func TestMenuAnswerHookSeenGoesIdle(t *testing.T) {
	tr, env := newTestTracker(t)
	env.run.set("tmux capture-pane", trustPane)
	discovered(t, tr, env, "")
	mustHook(t, tr, map[string]any{"hook_event_name": "Notification", "notification_type": "auth_success"})
	a := only(t, tr)
	if a.Status != Waiting {
		t.Fatalf("menu prompt: %+v", a)
	}
	if err := tr.Answer(context.Background(), a.ID, a.Waiting.ID, "2"); err != nil {
		t.Fatal(err)
	}
	if a := only(t, tr); a.Status != Idle {
		t.Fatalf("after the menu: %s", a.Status)
	}
}

// Fix 6: a panic while the lock is held does not wedge the tracker.
func TestPanicDoesNotWedgeLock(t *testing.T) {
	tr, _ := newTestTracker(t)
	orig := applyEvent
	applyEvent = func(*entry, Event, time.Time) (bool, bool) { panic("boom") }
	func() {
		defer func() { _ = recover() }()
		_ = tr.Hook(hookBody(t, map[string]any{"hook_event_name": "Stop"}))
	}()
	applyEvent = orig
	done := make(chan struct{})
	go func() { tr.Snapshot(); close(done) }()
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("tracker left locked")
	}
}

// Fix 7: keys are one digit 1-9.
func TestAnswerKeyTwoDigits(t *testing.T) {
	tr, env := newTestTracker(t)
	discovered(t, tr, env, "")
	permissionHook(t, tr, "make")
	tr.mu.Lock()
	e := tr.entries[agentID(sidA)]
	for i := 4; i <= 10; i++ {
		e.Waiting.Options = append(e.Waiting.Options, Option{Key: strconv.Itoa(i), Label: "x"})
	}
	tr.mu.Unlock()
	for _, k := range []string{"10", "0", "01"} {
		if err := tr.Answer(context.Background(), agentID(sidA), pid(tr, 1), k); !errors.Is(err, ErrBadKey) {
			t.Fatalf("key %q: %v", k, err)
		}
	}
}

// Fix 7: never more than nine options.
func TestOptionsCappedAtNine(t *testing.T) {
	var opts []string
	for i := 1; i <= 12; i++ {
		opts = append(opts, fmt.Sprintf(`{"label":"o%d"}`, i))
	}
	e := &entry{}
	in := `{"questions":[{"question":"Pick","options":[` + strings.Join(opts, ",") + `]}]}`
	apply(e, ev("PreToolUse", "tool", "AskUserQuestion", "input", in), hookNow)
	if e.Waiting == nil || len(e.Waiting.Options) != 9 || e.Waiting.Options[8].Key != "9" {
		t.Fatalf("question options %+v", e.Waiting)
	}
	var pane strings.Builder
	pane.WriteString(" Do you want to proceed?\n")
	for i := 1; i <= 11; i++ {
		fmt.Fprintf(&pane, "   %d. choice %d\n", i, i)
	}
	if got := ParseOptions(pane.String()); len(got) != 9 {
		t.Fatalf("pane options %d", len(got))
	}
}

// Fix 8: prompt ids of trackers started at different times never meet, and
// fit the card's 15-character id.
func TestPromptIDsDisjointAcrossRestarts(t *testing.T) {
	mk := func(at time.Time) []string {
		tr := NewTracker(TrackerDeps{Tmux: &Tmux{Bin: "tmux", Run: newFakeRun().run}, Discover: &fakeDisc{},
			Projects: t.TempDir(), Now: func() time.Time { return at }, After: func(time.Duration, func()) {}})
		var ids []string
		for range 50 {
			permissionHook(t, tr, "make")
			ids = append(ids, only(t, tr).Waiting.ID)
		}
		return ids
	}
	start := time.Unix(1_800_000_000, 0)
	a, b := mk(start), mk(start.Add(time.Second))
	for _, id := range a {
		if slices.Contains(b, id) {
			t.Fatalf("id %s in both", id)
		}
	}
	for _, id := range append(a, b...) {
		if len(id) > 15 {
			t.Fatalf("id %q too long", id)
		}
		for _, r := range id {
			if r < 0x21 || r > 0x7e {
				t.Fatalf("id %q not printable", id)
			}
		}
	}
}

// Fix 11: a line that never ends cannot grow Pending without bound.
func TestTailPendingCap(t *testing.T) {
	old := maxPending
	maxPending = 1024
	defer func() { maxPending = old }()
	path := filepath.Join(t.TempDir(), "s.jsonl")
	appendTo(t, path, `{"type":"user","message":{"content":"`+strings.Repeat("x", 2000))
	e := &entry{Transcript: path}
	tail(e, t0)
	if e.Pending != "" {
		t.Fatalf("pending kept %d bytes", len(e.Pending))
	}
	appendTo(t, path, strings.Repeat("y", 3000)+`"}}`+"\n"+`{"type":"user","gitBranch":"dev"}`+"\n")
	tail(e, t0)
	if e.Branch != "dev" || e.Pending != "" {
		t.Fatalf("after the long line: branch %q pending %q", e.Branch, e.Pending)
	}
}

// menuPane is a three-row arrow-key menu with the cursor on row cur.
func menuPane(cur int) string {
	rows := []string{"Yes", "Yes, allow all edits", "No"}
	var b strings.Builder
	b.WriteString("────────────────────────────\n Pick a mode\n")
	for i, r := range rows {
		if i == cur {
			b.WriteString(" ❯ " + r + "\n")
		} else {
			b.WriteString("   " + r + "\n")
		}
	}
	b.WriteString(" Enter to confirm · Esc to cancel")
	return b.String()
}

// Fix 12: menu answers move from wherever the cursor is now.
func TestMenuAnswerRelativeToCursor(t *testing.T) {
	cases := []struct {
		cur  int
		key  string
		want string
	}{
		{0, "3", "tmux send-keys -t %3 Down Down Enter"},
		{2, "1", "tmux send-keys -t %3 Up Up Enter"},
		{1, "2", "tmux send-keys -t %3 Enter"},
	}
	for _, c := range cases {
		tr, env := newTestTracker(t)
		env.run.set("tmux capture-pane", menuPane(0))
		discovered(t, tr, env, "")
		a := only(t, tr)
		if a.Status != Waiting || len(a.Waiting.Options) != 3 {
			t.Fatalf("menu: %+v", a.Waiting)
		}
		env.run.set("tmux capture-pane", menuPane(c.cur)) // the user moved the cursor
		if err := tr.Answer(context.Background(), a.ID, a.Waiting.ID, c.key); err != nil {
			t.Fatal(err)
		}
		if got := env.run.matching("tmux send-keys"); len(got) != 1 || got[0] != c.want {
			t.Errorf("cursor %d key %s: sent %q, want %q", c.cur, c.key, got, c.want)
		}
	}
}

// Fix 12: the menu is gone (answered at the terminal): nothing is typed.
func TestMenuAnswerStaleWhenMenuGone(t *testing.T) {
	for _, screen := range []string{"", permissionPane, trustPane} {
		tr, env := newTestTracker(t)
		env.run.set("tmux capture-pane", menuPane(0))
		discovered(t, tr, env, "")
		a := only(t, tr)
		env.run.set("tmux capture-pane", screen)
		if err := tr.Answer(context.Background(), a.ID, a.Waiting.ID, "2"); !errors.Is(err, ErrStale) {
			t.Fatalf("got %v", err)
		}
		if got := env.run.matching("tmux send-keys"); len(got) != 0 {
			t.Fatalf("sent %q", got)
		}
	}
}

// Fix 10: a failed claude lookup is retried, at most every 5 minutes.
func TestDiscoveryLookupRetried(t *testing.T) {
	f := newFakeRun()
	f.fail["/bin/zsh -lc command -v 'claude'"] = true
	f.set("/bin/ps -axww -o pid=,lstart=,tty=,args=", "")
	now := time.Unix(1_800_000_000, 0)
	d := &Discovery{Run: f.run, Projects: t.TempDir(), Now: func() time.Time { return now }}
	d.Sessions(context.Background())
	now = now.Add(4 * time.Minute)
	d.Sessions(context.Background())
	if n := len(f.matching("/bin/zsh")); n != 1 {
		t.Fatalf("lookups %d within 5 min", n)
	}
	now = now.Add(61 * time.Second)
	f.fail["/bin/zsh -lc command -v 'claude'"] = false
	f.set("/bin/zsh -lc command -v 'claude'", "/usr/local/bin/claude\n")
	f.set("/usr/local/bin/claude agents --json", "[]")
	d.Sessions(context.Background())
	if n := len(f.matching("/bin/zsh")); n != 2 {
		t.Fatalf("lookups %d after 5 min", n)
	}
	if n := len(f.matching("/usr/local/bin/claude agents")); n != 1 {
		t.Fatalf("claude agents ran %d times after the lookup recovered", n)
	}
}

// Fix 10: falling back to the process scan and recovering are logged once each.
func TestDiscoveryLogsFallbackOnce(t *testing.T) {
	f := newFakeRun()
	f.set("/bin/zsh -lc command -v 'claude'", "/usr/local/bin/claude\n")
	f.fail["/usr/local/bin/claude agents --json"] = true
	f.set("/bin/ps -axww -o pid=,lstart=,tty=,args=", "")
	now := time.Unix(1_800_000_000, 0)
	var mu sync.Mutex
	var logs []string
	d := &Discovery{Run: f.run, Projects: t.TempDir(), Now: func() time.Time { return now },
		Logf: func(format string, a ...any) { mu.Lock(); logs = append(logs, fmt.Sprintf(format, a...)); mu.Unlock() }}
	for range 3 {
		d.Sessions(context.Background())
		now = now.Add(11 * time.Second)
	}
	if len(logs) != 1 || !strings.Contains(logs[0], "process scan") {
		t.Fatalf("logs %q", logs)
	}
	f.fail["/usr/local/bin/claude agents --json"] = false
	f.set("/usr/local/bin/claude agents --json", "[]")
	for range 3 {
		d.Sessions(context.Background())
		now = now.Add(11 * time.Second)
	}
	if len(logs) != 2 || !strings.Contains(logs[1], "claude agents") {
		t.Fatalf("logs %q", logs)
	}
}

// Fix 10: with no agents for 10 minutes, `claude agents` runs once a minute.
func TestDiscoveryIdleBackoff(t *testing.T) {
	f := newFakeRun()
	f.set("/bin/zsh -lc command -v 'claude'", "/usr/local/bin/claude\n")
	f.set("/usr/local/bin/claude agents --json", "[]")
	now := time.Unix(1_800_000_000, 0)
	d := &Discovery{Run: f.run, Projects: t.TempDir(), Now: func() time.Time { return now }}
	runs := func() int { return len(f.matching("/usr/local/bin/claude agents")) }
	// First 10 minutes: every 10 s.
	for range 60 {
		d.Sessions(context.Background())
		now = now.Add(10 * time.Second)
	}
	if n := runs(); n != 60 {
		t.Fatalf("first 10 min: %d runs", n)
	}
	before := runs()
	for range 30 { // 5 more minutes at 10 s steps
		d.Sessions(context.Background())
		now = now.Add(10 * time.Second)
	}
	if n := runs() - before; n != 5 {
		t.Fatalf("idle: %d runs in 5 min, want 5", n)
	}
	// An agent shows up: back to every 10 s.
	f.set("/usr/local/bin/claude agents --json", agentsJSON())
	f.set("/bin/ps -o pid=,tty= -p", "")
	now = now.Add(time.Minute)
	d.Sessions(context.Background())
	before = runs()
	for range 6 {
		now = now.Add(10 * time.Second)
		d.Sessions(context.Background())
	}
	if n := runs() - before; n != 6 {
		t.Fatalf("busy again: %d runs in 1 min", n)
	}
}
