// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/tmux/tmux.go (run, SendKeys, Capture, PaneTTYs), on the user's default tmux server.

package agents

import (
	"context"
	"errors"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"time"
)

// Runner runs a command and returns its stdout: the seam tests replace.
type Runner func(ctx context.Context, name string, args ...string) ([]byte, error)

// execRunner runs the command for real, outside any project folder (nothing
// for claude to trust or read there).
func execRunner(ctx context.Context, name string, args ...string) ([]byte, error) {
	cmd := exec.CommandContext(ctx, name, args...)
	cmd.Dir = os.TempDir()
	out, err := cmd.Output()
	var ee *exec.ExitError
	if errors.As(err, &ee) && len(ee.Stderr) > 0 {
		err = errors.New(strings.TrimSpace(string(ee.Stderr)))
	}
	return out, err
}

const tmuxTimeout = 2 * time.Second

// Tmux talks to the user's default tmux server (never a private -L socket).
type Tmux struct {
	Bin string
	Run Runner
}

// NewTmux finds tmux in the usual places (launchd's PATH is minimal).
func NewTmux() *Tmux {
	bin := "tmux"
	for _, p := range []string{"/opt/homebrew/bin/tmux", "/usr/local/bin/tmux", "/usr/bin/tmux"} {
		if _, err := os.Stat(p); err == nil {
			bin = p
			break
		}
	}
	return &Tmux{Bin: bin, Run: execRunner}
}

func (t *Tmux) run(ctx context.Context, args ...string) (string, error) {
	ctx, cancel := context.WithTimeout(ctx, tmuxTimeout)
	defer cancel()
	out, err := t.Run(ctx, t.Bin, args...)
	return strings.TrimRight(string(out), "\n"), err
}

// Panes maps every pane's terminal ("/dev/ttys012") to its pane id ("%12");
// empty when no tmux server runs.
func (t *Tmux) Panes(ctx context.Context) map[string]string {
	panes := map[string]string{}
	// Space-separated: without a UTF-8 locale (launchd) tmux turns tabs into "_".
	out, err := t.run(ctx, "list-panes", "-a", "-F", "#{pane_tty} #{pane_id}")
	if err != nil {
		return panes
	}
	for _, l := range strings.Split(out, "\n") {
		f := strings.Fields(l)
		if len(f) == 2 && strings.HasPrefix(f[0], "/dev/") && strings.HasPrefix(f[1], "%") {
			panes[f[0]] = f[1]
		}
	}
	return panes
}

// Capture returns the visible pane text plus back lines of history.
func (t *Tmux) Capture(ctx context.Context, pane string, back int) string {
	out, _ := t.run(ctx, "capture-pane", "-p", "-J", "-t", pane, "-S", "-"+strconv.Itoa(max(back, 0)))
	return out
}

// SendKeys types literal (if any) into the pane, then presses keys.
func (t *Tmux) SendKeys(ctx context.Context, pane, literal string, keys ...string) error {
	if literal != "" {
		if _, err := t.run(ctx, "send-keys", "-t", pane, "-l", literal); err != nil {
			return err
		}
	}
	if len(keys) > 0 {
		if _, err := t.run(ctx, append([]string{"send-keys", "-t", pane}, keys...)...); err != nil {
			return err
		}
	}
	return nil
}
