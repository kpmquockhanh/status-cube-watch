package agents

import (
	"context"
	"errors"
	"slices"
	"strings"
	"sync"
	"testing"
)

// fakeRun records every command and answers from a table keyed by
// "name arg1 arg2 …" (exact), else by prefix; unknown commands fail.
type fakeRun struct {
	mu    sync.Mutex
	calls []string
	out   map[string]string
	fail  map[string]bool
	errs  map[string]error    // a specific error for a command (exact match)
	envs  map[string][]string // the extra environment each command last ran with
	// onCall, when set, runs before each command is answered (outside the
	// fake's lock): a slow command, or something happening meanwhile.
	onCall func(cmd string)
}

func newFakeRun() *fakeRun {
	return &fakeRun{out: map[string]string{}, fail: map[string]bool{}, errs: map[string]error{}, envs: map[string][]string{}}
}

func (f *fakeRun) setErr(cmd string, err error) { f.mu.Lock(); f.errs[cmd] = err; f.mu.Unlock() }

func (f *fakeRun) envOf(cmd string) []string { f.mu.Lock(); defer f.mu.Unlock(); return f.envs[cmd] }

func (f *fakeRun) set(cmd, out string) { f.mu.Lock(); f.out[cmd] = out; f.mu.Unlock() }

func (f *fakeRun) setFail(cmd string, fail bool) { f.mu.Lock(); f.fail[cmd] = fail; f.mu.Unlock() }

func (f *fakeRun) setOnCall(h func(string)) { f.mu.Lock(); f.onCall = h; f.mu.Unlock() }

func (f *fakeRun) run(ctx context.Context, name string, args ...string) ([]byte, error) {
	cmd := strings.Join(append([]string{name}, args...), " ")
	f.mu.Lock()
	hook := f.onCall
	f.mu.Unlock()
	if hook != nil {
		hook(cmd)
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	f.calls = append(f.calls, cmd)
	f.envs[cmd] = runEnv(ctx)
	if err := f.errs[cmd]; err != nil {
		return nil, err
	}
	if f.fail[cmd] {
		return nil, errors.New("failed")
	}
	if o, ok := f.out[cmd]; ok {
		return []byte(o), nil
	}
	best := ""
	for k := range f.out {
		if strings.HasPrefix(cmd, k) && len(k) > len(best) {
			best = k
		}
	}
	if best != "" {
		return []byte(f.out[best]), nil
	}
	return nil, errors.New("unexpected command: " + cmd)
}

// matching returns the recorded calls that start with prefix.
func (f *fakeRun) matching(prefix string) []string {
	f.mu.Lock()
	defer f.mu.Unlock()
	var out []string
	for _, c := range f.calls {
		if strings.HasPrefix(c, prefix) {
			out = append(out, c)
		}
	}
	return out
}

const listPanes = "tmux list-panes -a -F #{pane_tty} #{pane_id} #{session_id} #{window_index} #{pane_left} #{pane_top}"

func TestTmuxPanes(t *testing.T) {
	f := newFakeRun()
	f.set(listPanes, "/dev/ttys012 %12 $0 1 0 0\n/dev/ttys003 %3 $0 1 81 0\n\ngarbage\n")
	tm := &Tmux{Bin: "tmux", Run: f.run}
	got := tm.Panes(context.Background())
	if len(got) != 2 || got["/dev/ttys012"] != (Pane{"%12", 0}) || got["/dev/ttys003"] != (Pane{"%3", 1}) {
		t.Fatalf("panes %v", got)
	}

	// No server: the command fails, no panes.
	tm = &Tmux{Bin: "tmux", Run: newFakeRun().run}
	if got := tm.Panes(context.Background()); got == nil || len(got) != 0 {
		t.Fatalf("no server: %v", got)
	}
}

// Panes are ordered as they sit on screen: sessions in tmux's order, then
// windows, then left to right, then top to bottom, whatever their ids; a
// swap swaps the order.
func TestTmuxPaneOrder(t *testing.T) {
	f := newFakeRun()
	f.set(listPanes, strings.Join([]string{
		"/dev/ttys001 %1 $2 0 0 0",   // session $2 is listed first
		"/dev/ttys002 %7 $2 0 90 0",  // right column, top
		"/dev/ttys003 %4 $2 0 90 30", // right column, bottom
		"/dev/ttys004 %2 $2 0 45 0",  // middle
		"/dev/ttys005 %3 $2 1 0 0",   // second window
		"/dev/ttys006 %9 $0 0 0 0",   // a later session
	}, "\n"))
	tm := &Tmux{Bin: "tmux", Run: f.run}
	got := tm.Panes(context.Background())
	want := map[string]int{"%1": 0, "%2": 1, "%7": 2, "%4": 3, "%3": 4, "%9": 5}
	for _, p := range got {
		if want[p.ID] != p.Order {
			t.Errorf("%s at %d, want %d", p.ID, p.Order, want[p.ID])
		}
	}

	// Swap %1 and %2: the two panes trade places, so their order follows.
	f.set(listPanes, "/dev/ttys004 %2 $2 0 0 0\n/dev/ttys001 %1 $2 0 45 0\n")
	got = tm.Panes(context.Background())
	if got["/dev/ttys004"].Order != 0 || got["/dev/ttys001"].Order != 1 {
		t.Fatalf("after swap %v", got)
	}
}

func TestTmuxSendKeysAndCapture(t *testing.T) {
	f := newFakeRun()
	f.set("tmux send-keys", "")
	f.set("tmux capture-pane", "screen")
	tm := &Tmux{Bin: "tmux", Run: f.run}
	if err := tm.SendKeys(context.Background(), "%12", "1"); err != nil {
		t.Fatal(err)
	}
	if got := f.matching("tmux send-keys"); len(got) != 1 || got[0] != "tmux send-keys -t %12 -l 1" {
		t.Fatalf("literal: %q", got)
	}
	if err := tm.SendKeys(context.Background(), "%12", "", "Up", "Enter"); err != nil {
		t.Fatal(err)
	}
	if got := f.matching("tmux send-keys"); len(got) != 2 || got[1] != "tmux send-keys -t %12 Up Enter" {
		t.Fatalf("keys: %q", got)
	}
	if got := tm.Capture(context.Background(), "%12", 40); got != "screen" {
		t.Fatalf("capture %q", got)
	}
	if got := f.matching("tmux capture-pane"); len(got) != 1 || got[0] != "tmux capture-pane -p -J -t %12 -S -40" {
		t.Fatalf("capture args: %q", got)
	}
	tm.Panes(context.Background())
	for _, c := range f.calls {
		for _, a := range strings.Fields(c) {
			if a == "-L" {
				t.Fatalf("default server only, got %q", c)
			}
		}
	}
}

func TestTmuxSendKeysError(t *testing.T) {
	tm := &Tmux{Bin: "tmux", Run: newFakeRun().run}
	if err := tm.SendKeys(context.Background(), "%1", "1"); err == nil {
		t.Fatal("a failed send-keys must report an error")
	}
}

// Fix 9: tmux runs with a UTF-8 locale, so capture-pane keeps ❯ and │ under
// launchd's minimal environment.
func TestTmuxUTF8Env(t *testing.T) {
	f := newFakeRun()
	f.set("tmux", "")
	tm := &Tmux{Bin: "tmux", Run: f.run}
	tm.Panes(context.Background())
	tm.Capture(context.Background(), "%1", 0)
	_ = tm.SendKeys(context.Background(), "%1", "1", "Enter")
	if len(f.calls) != 4 {
		t.Fatalf("calls %q", f.calls)
	}
	for _, c := range f.calls {
		if env := f.envOf(c); !slices.Contains(env, "LC_CTYPE=en_US.UTF-8") {
			t.Errorf("%s ran with env %q", c, env)
		}
	}
}

// The real runner passes that environment on (checked with /usr/bin/env).
func TestExecRunnerEnv(t *testing.T) {
	out, err := execRunner(withRunEnv(context.Background(), "CUBE_TEST_ENV=x1"), "/usr/bin/env")
	if err != nil {
		t.Fatal(err)
	}
	// Report only the checks: the environment may hold secrets.
	if !strings.Contains(string(out), "CUBE_TEST_ENV=x1\n") || !strings.Contains(string(out), "PATH=") {
		t.Fatal("the extra variable or the inherited PATH is missing")
	}
}

func TestNewTmuxBin(t *testing.T) {
	tm := NewTmux()
	if tm.Run == nil || tm.Bin == "" {
		t.Fatalf("%+v", tm)
	}
}
