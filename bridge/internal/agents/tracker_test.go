package agents

import (
	"context"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"sync"
	"syscall"
	"testing"
	"time"
)

type fakeDisc struct {
	mu    sync.Mutex
	lives []Live // nil = discovery failed
}

func (f *fakeDisc) set(l []Live) { f.mu.Lock(); f.lives = l; f.mu.Unlock() }

func (f *fakeDisc) Sessions(context.Context) []Live {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.lives == nil {
		return nil
	}
	return append([]Live{}, f.lives...)
}

type testEnv struct {
	run      *fakeRun
	disc     *fakeDisc
	projects string
	mu       sync.Mutex
	now      time.Time
}

func (e *testEnv) advance(d time.Duration) { e.mu.Lock(); e.now = e.now.Add(d); e.mu.Unlock() }

func newTestTracker(t *testing.T) (*Tracker, *testEnv) {
	t.Helper()
	env := &testEnv{run: newFakeRun(), disc: &fakeDisc{}, projects: t.TempDir(), now: time.Now()}
	env.run.set("tmux list-panes -a -F #{pane_tty} #{pane_id}", "/dev/ttys003 %3\n")
	env.run.set("tmux capture-pane", "")
	env.run.set("tmux send-keys", "")
	tr := NewTracker(TrackerDeps{
		Tmux:     &Tmux{Bin: "tmux", Run: env.run.run},
		Discover: env.disc,
		Projects: env.projects,
		Now:      func() time.Time { env.mu.Lock(); defer env.mu.Unlock(); return env.now },
		After:    func(_ time.Duration, f func()) { f() },
	})
	return tr, env
}

func hookBody(t *testing.T, kv map[string]any) []byte {
	t.Helper()
	if _, ok := kv["session_id"]; !ok {
		kv["session_id"] = sidA
	}
	if _, ok := kv["cwd"]; !ok {
		kv["cwd"] = "/work/proj"
	}
	b, err := json.Marshal(kv)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func mustHook(t *testing.T, tr *Tracker, kv map[string]any) {
	t.Helper()
	if err := tr.Hook(hookBody(t, kv)); err != nil {
		t.Fatal(err)
	}
}

func only(t *testing.T, tr *Tracker) Agent {
	t.Helper()
	a := tr.Snapshot()
	if len(a) != 1 {
		t.Fatalf("want one agent, got %+v", a)
	}
	return a[0]
}

func TestHookCreatesAgent(t *testing.T) {
	tr, _ := newTestTracker(t)
	mustHook(t, tr, map[string]any{"hook_event_name": "SessionStart", "source": "startup"})
	a := only(t, tr)
	if a.ID != agentID(sidA) || a.SessionID != sidA || a.Folder != "proj" || a.Cwd != "/work/proj" || a.Status != Idle || a.Title != "proj" {
		t.Fatalf("%+v", a)
	}
	if err := tr.Hook([]byte("{not json")); err == nil {
		t.Fatal("a bad body is an error")
	}
	if len(tr.Snapshot()) != 1 {
		t.Fatal("a bad body changes nothing")
	}
}

func TestSubagentHookIgnored(t *testing.T) {
	tr, env := newTestTracker(t)
	p := filepath.Join(env.projects, "-work-proj", sidA, "subagents", "agent-x.jsonl")
	mustHook(t, tr, map[string]any{"hook_event_name": "PreToolUse", "tool_name": "Bash", "transcript_path": p})
	if a := tr.Snapshot(); len(a) != 0 {
		t.Fatalf("a subagent's hook created %+v", a)
	}
}

// discovered puts sidA on tty ttys003 (pane %3) and syncs once.
func discovered(t *testing.T, tr *Tracker, env *testEnv, transcript string) {
	t.Helper()
	env.disc.set([]Live{{PID: 1, TTY: "/dev/ttys003", Cwd: "/work/proj", SessionID: sidA, Transcript: transcript}})
	tr.Sync(context.Background())
}

func TestPermissionFlow(t *testing.T) {
	tr, env := newTestTracker(t)
	discovered(t, tr, env, "")
	env.run.set("tmux capture-pane", permissionPane)
	mustHook(t, tr, map[string]any{"hook_event_name": "PermissionRequest", "tool_name": "Bash",
		"tool_input": map[string]string{"command": "terraform apply"}})
	a := only(t, tr)
	if a.Status != Waiting || a.Waiting == nil || a.Waiting.ID != "p1" || a.Waiting.Detail != "terraform apply" || a.Pane != "%3" {
		t.Fatalf("%+v %+v", a, a.Waiting)
	}
	want := []string{"yes", "yes, and always allow access to /tmp/at-proj from this project", "no"}
	if len(a.Waiting.Options) != 3 {
		t.Fatalf("options %+v", a.Waiting.Options)
	}
	for i, w := range want {
		if a.Waiting.Options[i].Label != w {
			t.Errorf("option %d = %q, want %q", i, a.Waiting.Options[i].Label, w)
		}
	}
	// After the menu scan of the starting agent (-S -0): two re-scans of the prompt.
	if got := env.run.matching("tmux capture-pane -p -J -t %3 -S -40"); len(got) != 2 {
		t.Fatalf("re-scans %q", env.run.matching("tmux capture-pane"))
	}
}

func TestAnswer(t *testing.T) {
	tr, env := newTestTracker(t)
	discovered(t, tr, env, "")
	id := agentID(sidA)
	mustHook(t, tr, map[string]any{"hook_event_name": "PermissionRequest", "tool_name": "Bash",
		"tool_input": map[string]string{"command": "terraform apply"}})
	if err := tr.Answer(context.Background(), id, "p1", "1"); err != nil {
		t.Fatal(err)
	}
	if got := env.run.matching("tmux send-keys"); len(got) != 1 || got[0] != "tmux send-keys -t %3 -l 1" {
		t.Fatalf("sent %q", got)
	}
	if a := only(t, tr); a.Status != Running || a.Waiting != nil {
		t.Fatalf("after yes: %+v", a)
	}
	mustHook(t, tr, map[string]any{"hook_event_name": "PermissionRequest", "tool_name": "Bash",
		"tool_input": map[string]string{"command": "rm -rf /"}})
	if err := tr.Answer(context.Background(), id, "p2", "3"); err != nil {
		t.Fatal(err)
	}
	if a := only(t, tr); a.Status != Idle {
		t.Fatalf("after no: %+v", a)
	}
}

func TestAnswerStale(t *testing.T) {
	tr, env := newTestTracker(t)
	discovered(t, tr, env, "")
	id := agentID(sidA)
	mustHook(t, tr, map[string]any{"hook_event_name": "PermissionRequest", "tool_name": "Bash",
		"tool_input": map[string]string{"command": "terraform apply"}})
	mustHook(t, tr, map[string]any{"hook_event_name": "PostToolUse", "tool_name": "Bash"})
	if err := tr.Answer(context.Background(), id, "p1", "1"); !errors.Is(err, ErrStale) {
		t.Fatalf("answered at the terminal: %v", err)
	}
	mustHook(t, tr, map[string]any{"hook_event_name": "PermissionRequest", "tool_name": "Bash",
		"tool_input": map[string]string{"command": "make"}})
	if err := tr.Answer(context.Background(), id, "p1", "1"); !errors.Is(err, ErrStale) {
		t.Fatalf("a newer prompt: %v", err)
	}
	for _, k := range []string{"9", "", "1; rm -rf ~", "Enter"} {
		if err := tr.Answer(context.Background(), id, "p2", k); !errors.Is(err, ErrBadKey) {
			t.Fatalf("key %q: %v", k, err)
		}
	}
	if err := tr.Answer(context.Background(), "nobody", "p2", "1"); !errors.Is(err, ErrNotFound) {
		t.Fatalf("unknown agent: %v", err)
	}
	if got := env.run.matching("tmux send-keys"); len(got) != 0 {
		t.Fatalf("nothing may be typed, got %q", got)
	}
}

func TestAnswerNotInTmux(t *testing.T) {
	tr, env := newTestTracker(t)
	mustHook(t, tr, map[string]any{"hook_event_name": "PermissionRequest", "tool_name": "Bash",
		"tool_input": map[string]string{"command": "make"}})
	if err := tr.Answer(context.Background(), agentID(sidA), "p1", "1"); !errors.Is(err, ErrNotInTmux) {
		t.Fatalf("got %v", err)
	}
	if got := env.run.matching("tmux send-keys"); len(got) != 0 {
		t.Fatalf("sent %q", got)
	}
}

func TestMenuAnswer(t *testing.T) {
	tr, env := newTestTracker(t)
	env.run.set("tmux capture-pane", trustPane)
	// A fresh session in an untrusted folder: no transcript yet.
	discovered(t, tr, env, filepath.Join(env.projects, "-work-proj", sidA+".jsonl"))
	a := only(t, tr)
	if a.Status != Waiting || a.Waiting == nil || len(a.Waiting.Options) != 2 || a.Waiting.Title != "trust this folder?" {
		t.Fatalf("%+v %+v", a, a.Waiting)
	}
	if err := tr.Answer(context.Background(), a.ID, a.Waiting.ID, "2"); err != nil {
		t.Fatal(err)
	}
	if got := env.run.matching("tmux send-keys"); len(got) != 1 || got[0] != "tmux send-keys -t %3 Up Up Down Enter" {
		t.Fatalf("sent %q", got)
	}
	if a := only(t, tr); a.Status != Starting {
		t.Fatalf("after the menu: %+v", a)
	}
}

func writeLines(t *testing.T, path string, lines ...string) {
	t.Helper()
	_ = os.MkdirAll(filepath.Dir(path), 0o700)
	f, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o600)
	if err != nil {
		t.Fatal(err)
	}
	for _, l := range lines {
		f.WriteString(l + "\n")
	}
	f.Close()
}

func TestHooklessStatus(t *testing.T) {
	tr, env := newTestTracker(t)
	tp := filepath.Join(env.projects, "-work-proj", sidA+".jsonl")
	writeLines(t, tp,
		`{"type":"user","message":{"content":"hi"}}`,
		`{"type":"assistant","message":{"id":"m1","model":"claude-opus-5-5","content":[{"type":"tool_use","name":"Grep"}],"usage":{"input_tokens":20,"output_tokens":5}}}`)
	discovered(t, tr, env, tp)
	a := only(t, tr)
	if a.Status != Running || a.Activity == nil || a.Activity.Tool != "Grep" || a.ModelLabel != "opus 5.5" {
		t.Fatalf("running: %+v %+v", a, a.Activity)
	}
	writeLines(t, tp,
		`{"type":"user","message":{"content":[{"type":"tool_result","content":"ok"}]}}`,
		`{"type":"assistant","message":{"id":"m2","model":"claude-opus-5-5","stop_reason":"end_turn","content":[{"type":"text","text":"all done"}],"usage":{"input_tokens":30,"output_tokens":5}}}`)
	env.advance(11 * time.Minute)
	tr.Sync(context.Background())
	if a := only(t, tr); a.Status != Idle || !a.Unseen {
		t.Fatalf("finished while watched: %+v", a)
	}

	// Claude Code's own report: blocked means it needs you.
	env.disc.set([]Live{{TTY: "/dev/ttys003", Cwd: "/work/proj", SessionID: sidA, Transcript: tp, Reported: true, Busy: true, Blocked: true}})
	tr.Sync(context.Background())
	if a := only(t, tr); a.Status != Waiting || a.Waiting == nil || a.Waiting.Kind != "input" {
		t.Fatalf("blocked: %+v", a)
	}

	// Once a hook has been seen, hooks own the status.
	mustHook(t, tr, map[string]any{"hook_event_name": "Stop"})
	env.disc.set([]Live{{TTY: "/dev/ttys003", Cwd: "/work/proj", SessionID: sidA, Transcript: tp, Reported: true, Busy: true}})
	tr.Sync(context.Background())
	if a := only(t, tr); a.Status != Idle {
		t.Fatalf("hook seen: %+v", a)
	}
}

func TestExpiry(t *testing.T) {
	tr, env := newTestTracker(t)
	// Exited for 61 s: gone (discovery failing throughout).
	mustHook(t, tr, map[string]any{"hook_event_name": "SessionStart"})
	mustHook(t, tr, map[string]any{"hook_event_name": "SessionEnd"})
	tr.Sync(context.Background())
	if a := only(t, tr); a.Status != Exited {
		t.Fatalf("%+v", a)
	}
	env.advance(61 * time.Second)
	tr.Sync(context.Background())
	if len(tr.Snapshot()) != 0 {
		t.Fatal("exited for 61 s: still there")
	}

	// Missing from two successful discoveries: gone.
	env.disc.set([]Live{{Cwd: "/work/b", SessionID: sidB}})
	tr.Sync(context.Background())
	only(t, tr)
	env.disc.set([]Live{})
	tr.Sync(context.Background())
	only(t, tr)
	env.disc.set(nil) // failed discoveries in between remove nothing and keep the count
	for range 3 {
		tr.Sync(context.Background())
	}
	only(t, tr)
	env.disc.set([]Live{})
	tr.Sync(context.Background())
	if len(tr.Snapshot()) != 0 {
		t.Fatal("missing twice: still there")
	}

	// A session that just sent a hook is alive even if a (cached) discovery
	// has not seen it yet.
	mustHook(t, tr, map[string]any{"hook_event_name": "UserPromptSubmit", "session_id": sidC})
	tr.Sync(context.Background())
	tr.Sync(context.Background())
	only(t, tr)
	env.advance(16 * time.Second)
	tr.Sync(context.Background())
	tr.Sync(context.Background())
	if len(tr.Snapshot()) != 0 {
		t.Fatal("quiet and missing twice: still there")
	}
}

func TestSubscribe(t *testing.T) {
	tr, _ := newTestTracker(t)
	ch, cancel := tr.Subscribe()
	defer cancel()
	mustHook(t, tr, map[string]any{"hook_event_name": "SessionStart"})
	select {
	case <-ch:
	default:
		t.Fatal("a change must tick")
	}
	mustHook(t, tr, map[string]any{"hook_event_name": "Notification", "notification_type": "auth_success"})
	select {
	case <-ch:
		t.Fatal("a no-op hook must not tick")
	default:
	}
	// Many changes never block the tracker.
	for range 5 {
		mustHook(t, tr, map[string]any{"hook_event_name": "UserPromptSubmit", "prompt": "x"})
		mustHook(t, tr, map[string]any{"hook_event_name": "Stop"})
	}
	cancel()
	mustHook(t, tr, map[string]any{"hook_event_name": "UserPromptSubmit", "prompt": "y"})
}

func TestSnapshotIsACopy(t *testing.T) {
	tr, _ := newTestTracker(t)
	mustHook(t, tr, map[string]any{"hook_event_name": "PermissionRequest", "tool_name": "Bash",
		"tool_input": map[string]string{"command": "make"}})
	a := only(t, tr)
	a.Waiting.Options[0].Label = "changed"
	if b := only(t, tr); b.Waiting.Options[0].Label == "changed" {
		t.Fatal("snapshot shares the prompt")
	}
}

func transcriptOf(tr *Tracker, sid string) string {
	tr.mu.Lock()
	defer tr.mu.Unlock()
	if e := tr.entries[agentID(sid)]; e != nil {
		return e.Transcript
	}
	return "<none>"
}

func TestTranscriptPathValidated(t *testing.T) {
	tr, env := newTestTracker(t)
	good := filepath.Join(env.projects, "-work-proj", sidA+".jsonl")
	writeLines(t, good, `{}`)
	outside := filepath.Join(t.TempDir(), sidB+".jsonl")
	writeLines(t, outside, `{}`)
	fifo := filepath.Join(env.projects, "-work-proj", sidB+".jsonl")
	if err := syscall.Mkfifo(fifo, 0o600); err != nil {
		t.Fatal(err)
	}
	link := filepath.Join(env.projects, "-work-proj", sidC+".jsonl")
	if err := os.Symlink(good, link); err != nil {
		t.Fatal(err)
	}
	linkDir := filepath.Join(env.projects, "-elsewhere")
	if err := os.Symlink(filepath.Dir(outside), linkDir); err != nil {
		t.Fatal(err)
	}
	notJSONL := filepath.Join(env.projects, "-work-proj", "notes.txt")
	writeLines(t, notJSONL, "x")

	cases := []struct {
		path string
		ok   bool
	}{
		{good, true},
		{outside, false},
		{fifo, false},
		{link, false},
		{filepath.Join(linkDir, sidB+".jsonl"), false},
		{notJSONL, false},
		{"-work-proj/" + sidA + ".jsonl", false},
		{filepath.Join(env.projects, "-work-proj", "..", "..", filepath.Base(filepath.Dir(outside)), sidB+".jsonl"), false},
	}
	for i, c := range cases {
		sid := []string{sidA, sidB, sidC}[i%3]
		tr.mu.Lock()
		delete(tr.entries, agentID(sid))
		tr.mu.Unlock()
		mustHook(t, tr, map[string]any{"hook_event_name": "SessionStart", "session_id": sid, "transcript_path": c.path})
		got := transcriptOf(tr, sid)
		if c.ok && got != c.path || !c.ok && got != "" {
			t.Errorf("case %d (%s): transcript %q", i, c.path, got)
		}
	}
	// The FIFO never blocks a Sync either.
	done := make(chan struct{})
	go func() { tr.Sync(context.Background()); close(done) }()
	select {
	case <-done:
	case <-time.After(5 * time.Second):
		t.Fatal("Sync blocked")
	}
}

// Hooks, Syncs, answers and snapshots at once, with the re-scan on its own
// goroutines: go test -race must stay clean.
func TestConcurrentUse(t *testing.T) {
	env := &testEnv{run: newFakeRun(), disc: &fakeDisc{}, projects: t.TempDir(), now: time.Now()}
	env.run.set("tmux list-panes -a -F #{pane_tty} #{pane_id}", "/dev/ttys003 %3\n")
	env.run.set("tmux capture-pane", permissionPane)
	env.run.set("tmux send-keys", "")
	tp := filepath.Join(env.projects, "-work-proj", sidA+".jsonl")
	writeLines(t, tp, `{"type":"assistant","message":{"id":"m1","model":"claude-opus-5-5","usage":{"input_tokens":5}}}`)
	env.disc.set([]Live{{TTY: "/dev/ttys003", Cwd: "/work/proj", SessionID: sidA, Transcript: tp}})
	var bg sync.WaitGroup
	tr := NewTracker(TrackerDeps{
		Tmux: &Tmux{Bin: "tmux", Run: env.run.run}, Discover: env.disc, Projects: env.projects,
		Now:   func() time.Time { env.mu.Lock(); defer env.mu.Unlock(); return env.now },
		After: func(_ time.Duration, f func()) { bg.Add(1); go func() { defer bg.Done(); f() }() },
	})
	ch, cancel := tr.Subscribe()
	defer cancel()
	var wg sync.WaitGroup
	for g := range 4 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := range 30 {
				switch (g + i) % 4 {
				case 0:
					_ = tr.Hook(hookBody(t, map[string]any{"hook_event_name": "PermissionRequest", "tool_name": "Bash", "transcript_path": tp,
						"tool_input": map[string]string{"command": "make"}}))
				case 1:
					tr.Sync(context.Background())
				case 2:
					for _, a := range tr.Snapshot() {
						if a.Waiting != nil {
							_ = tr.Answer(context.Background(), a.ID, a.Waiting.ID, "1")
						}
					}
				case 3:
					writeLines(t, tp, `{"type":"assistant","message":{"id":"m2","model":"claude-opus-5-5","usage":{"input_tokens":7}}}`)
					select {
					case <-ch:
					default:
					}
				}
			}
		}()
	}
	wg.Wait()
	bg.Wait()
	only(t, tr)
}

func TestMergeTailHookWins(t *testing.T) {
	path := filepath.Join(t.TempDir(), "s.jsonl")
	writeLines(t, path, `{"type":"assistant","isApiErrorMessage":true,"message":{"id":"x","content":[{"type":"text","text":"API Error: 529 Overloaded"}]}}`)
	now := time.Now()
	tm := &Tmux{Run: newFakeRun().run}
	e := &entry{Transcript: path}
	e.SetStatus(Running, now)
	j := syncJob{snap: *e}
	j.run(context.Background(), tm, now)
	if j.tailed.Status != Error {
		t.Fatalf("tail on the copy: %s", j.tailed.Status)
	}
	// A hook arrived while the job ran: its status wins, the read is kept.
	e.SetStatus(Idle, now)
	mergeTail(e, &j)
	if e.Status != Idle || e.Offset == 0 {
		t.Fatalf("hook should win: %s offset %d", e.Status, e.Offset)
	}
	// No hook meanwhile: tail's status applies.
	e2 := &entry{Transcript: path}
	e2.SetStatus(Running, now)
	j2 := syncJob{snap: *e2}
	j2.run(context.Background(), tm, now)
	mergeTail(e2, &j2)
	if e2.Status != Error || e2.ErrorText == "" {
		t.Fatalf("tail status: %s %q", e2.Status, e2.ErrorText)
	}
	// The transcript was reset meanwhile: nothing from the old read.
	e3 := &entry{Transcript: path}
	j3 := syncJob{snap: *e3}
	j3.run(context.Background(), tm, now)
	e3.Transcript = path + ".new"
	if mergeTail(e3, &j3) || e3.Offset != 0 {
		t.Fatal("a stale read must be dropped")
	}
}
