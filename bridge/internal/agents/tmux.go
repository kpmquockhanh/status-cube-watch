// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/tmux/tmux.go (run, SendKeys, Capture, PaneTTYs), on the user's default tmux server.

package agents

import (
	"context"
	"errors"
	"os"
	"os/exec"
	"sort"
	"strconv"
	"strings"
	"time"
)

// Runner runs a command and returns its stdout: the seam tests replace.
type Runner func(ctx context.Context, name string, args ...string) ([]byte, error)

type runEnvKey struct{}

// withRunEnv asks the Runner to add env ("KEY=value") to the command's
// inherited environment.
func withRunEnv(ctx context.Context, env ...string) context.Context {
	return context.WithValue(ctx, runEnvKey{}, append(runEnv(ctx), env...))
}

// runEnv is the extra environment withRunEnv put in ctx.
func runEnv(ctx context.Context) []string {
	env, _ := ctx.Value(runEnvKey{}).([]string)
	return env
}

// execRunner runs the command for real, outside any project folder (nothing
// for claude to trust or read there), with the extra environment of ctx.
func execRunner(ctx context.Context, name string, args ...string) ([]byte, error) {
	cmd := exec.CommandContext(ctx, name, args...)
	cmd.Dir = os.TempDir()
	if env := runEnv(ctx); len(env) > 0 {
		cmd.Env = append(os.Environ(), env...) // a later duplicate wins
	}
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

// tmuxEnv: launchd gives the bridge no locale, and a tmux client without a
// UTF-8 one turns ❯ and │ into "_" in capture-pane, which hides every dialog.
var tmuxEnv = []string{"LC_CTYPE=en_US.UTF-8"}

func (t *Tmux) run(ctx context.Context, args ...string) (string, error) {
	ctx, cancel := context.WithTimeout(withRunEnv(ctx, tmuxEnv...), tmuxTimeout)
	defer cancel()
	out, err := t.Run(ctx, t.Bin, args...)
	return strings.TrimRight(string(out), "\n"), err
}

// Pane is a tmux pane: its id ("%12") and where it sits among all of them
// (Order 0 first): sessions in tmux's own order, then windows by index, then
// left to right, then top to bottom. Swapping two panes swaps their Order.
type Pane struct {
	ID    string
	Order int
}

// Panes maps every pane's terminal ("/dev/ttys012") to its pane; empty when
// no tmux server runs.
func (t *Tmux) Panes(ctx context.Context) map[string]Pane {
	panes := map[string]Pane{}
	// Space-separated: without a UTF-8 locale (launchd) tmux turns tabs into "_".
	// Every field is free of spaces (session_id, not the session's name).
	out, err := t.run(ctx, "list-panes", "-a", "-F",
		"#{pane_tty} #{pane_id} #{session_id} #{window_index} #{pane_left} #{pane_top}")
	if err != nil {
		return panes
	}
	type place struct {
		tty, id                    string
		session, window, left, top int
	}
	var all []place
	sessions := map[string]int{} // session id -> rank, in the order tmux lists them
	for _, l := range strings.Split(out, "\n") {
		f := strings.Fields(l)
		if len(f) < 2 || !strings.HasPrefix(f[0], "/dev/") || !strings.HasPrefix(f[1], "%") {
			continue
		}
		p := place{tty: f[0], id: f[1]}
		if len(f) == 6 {
			if _, ok := sessions[f[2]]; !ok {
				sessions[f[2]] = len(sessions)
			}
			p.session = sessions[f[2]]
			p.window, _ = strconv.Atoi(f[3])
			p.left, _ = strconv.Atoi(f[4])
			p.top, _ = strconv.Atoi(f[5])
		}
		all = append(all, p)
	}
	sort.SliceStable(all, func(i, j int) bool {
		a, b := all[i], all[j]
		if a.session != b.session {
			return a.session < b.session
		}
		if a.window != b.window {
			return a.window < b.window
		}
		if a.left != b.left {
			return a.left < b.left
		}
		return a.top < b.top
	})
	for i, p := range all {
		panes[p.tty] = Pane{ID: p.id, Order: i}
	}
	return panes
}

// Capture returns the visible pane text plus back lines of history.
func (t *Tmux) Capture(ctx context.Context, pane string, back int) string {
	out, _ := t.run(ctx, "capture-pane", "-p", "-J", "-t", pane, "-S", "-"+strconv.Itoa(max(back, 0)))
	return out
}

// sentNothing reports whether a failed tmux command certainly typed nothing:
// tmux refused it up front (no server, no such pane) or could not be started.
// A timeout, a kill or any other failure may have come after the keys landed.
func sentNothing(err error) bool {
	if err == nil || errors.Is(err, context.DeadlineExceeded) || errors.Is(err, context.Canceled) {
		return false
	}
	if errors.Is(err, exec.ErrNotFound) || errors.Is(err, os.ErrNotExist) {
		return true
	}
	msg := err.Error()
	for _, refused := range []string{"can't find pane", "can't find window", "can't find session",
		"no server running", "error connecting to"} {
		if strings.Contains(msg, refused) {
			return true
		}
	}
	return false
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
