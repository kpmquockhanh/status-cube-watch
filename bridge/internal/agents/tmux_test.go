package agents

import (
	"context"
	"errors"
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
}

func newFakeRun() *fakeRun { return &fakeRun{out: map[string]string{}, fail: map[string]bool{}} }

func (f *fakeRun) set(cmd, out string) { f.mu.Lock(); f.out[cmd] = out; f.mu.Unlock() }

func (f *fakeRun) run(_ context.Context, name string, args ...string) ([]byte, error) {
	cmd := strings.Join(append([]string{name}, args...), " ")
	f.mu.Lock()
	defer f.mu.Unlock()
	f.calls = append(f.calls, cmd)
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

func TestTmuxPanes(t *testing.T) {
	f := newFakeRun()
	f.set("tmux list-panes -a -F #{pane_tty} #{pane_id}", "/dev/ttys012 %12\n/dev/ttys003 %3\n\ngarbage\n")
	tm := &Tmux{Bin: "tmux", Run: f.run}
	got := tm.Panes(context.Background())
	if len(got) != 2 || got["/dev/ttys012"] != "%12" || got["/dev/ttys003"] != "%3" {
		t.Fatalf("panes %v", got)
	}

	// No server: the command fails, no panes.
	tm = &Tmux{Bin: "tmux", Run: newFakeRun().run}
	if got := tm.Panes(context.Background()); got == nil || len(got) != 0 {
		t.Fatalf("no server: %v", got)
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

func TestNewTmuxBin(t *testing.T) {
	tm := NewTmux()
	if tm.Run == nil || tm.Bin == "" {
		t.Fatalf("%+v", tm)
	}
}
