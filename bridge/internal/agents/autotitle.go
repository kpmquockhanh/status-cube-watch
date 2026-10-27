package agents

import (
	"context"
	"errors"
	"strings"
	"time"
)

// Claude Code names a session once, from its first prompt, so a session
// that moved on keeps a stale name. The tracker renames it from the prompts
// typed most recently: when they change, at most every autoTitleEvery, one
// request at a time. A rename (/rename) still wins over it.
const (
	autoTitleEvery   = 5 * time.Minute
	autoTitleMin     = 2 // prompts before a title of our own: one is what Claude's title is made from
	autoTitleTimeout = 60 * time.Second
	autoTitleChars   = 40
)

// Titler makes a short title from a session's recent prompts, oldest first.
type Titler func(ctx context.Context, prompts []string) (string, error)

// titleDue: e has prompts to title that its title was not made from, and
// the last attempt is long enough ago.
func titleDue(e *entry, now time.Time) bool {
	folder := e.Folder
	if e.Status == Exited || (e.CustomTitle != "" && e.CustomTitle != folder) || len(e.Recent) < autoTitleMin {
		return false
	}
	return recentKey(e.Recent) != e.AutoFor && (e.AutoAt.IsZero() || now.Sub(e.AutoAt) >= autoTitleEvery)
}

func recentKey(recent []string) string { return strings.Join(recent, "\n") }

// autoTitle starts a title request for the agent that has waited longest
// for one, unless one is already out.
func (t *Tracker) autoTitle(ctx context.Context, now time.Time) {
	if t.d.Title == nil {
		return
	}
	id, prompts := t.claimTitle(now)
	if id == "" {
		return
	}
	t.d.Spawn(func() {
		rctx, cancel := context.WithTimeout(ctx, autoTitleTimeout)
		defer cancel()
		title, err := t.d.Title(rctx, prompts)
		if err == nil {
			title = cleanTitle(title)
			if title == "" {
				err = errors.New("empty title")
			}
		}
		if err != nil && t.d.Log != nil {
			t.d.Log("auto title for " + id + " failed: " + clip(oneLine(err.Error()), 200))
		}
		if t.finishTitle(id, prompts, title, err == nil) {
			t.notify()
		}
	})
}

// claimTitle picks the due agent with the oldest attempt and marks a request out.
func (t *Tracker) claimTitle(now time.Time) (string, []string) {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.titling {
		return "", nil
	}
	var pick *entry
	for _, e := range t.entries {
		if titleDue(e, now) && (pick == nil || e.AutoAt.Before(pick.AutoAt) || (e.AutoAt.Equal(pick.AutoAt) && e.ID < pick.ID)) {
			pick = e
		}
	}
	if pick == nil {
		return "", nil
	}
	t.titling, pick.AutoAt = true, now
	return pick.ID, append([]string(nil), pick.Recent...)
}

// finishTitle stores a title made from prompts (ok), or only ends the
// request (a failure is tried again after autoTitleEvery).
func (t *Tracker) finishTitle(id string, prompts []string, title string, ok bool) bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.titling = false
	e := t.entries[id]
	if e == nil || !ok {
		return false
	}
	e.AutoTitle, e.AutoFor = title, recentKey(prompts)
	return retitle(e)
}

// cleanTitle is the model's first line without quotes, a "Title:" label or
// a final full stop, at most autoTitleChars.
func cleanTitle(s string) string {
	for _, l := range strings.Split(s, "\n") {
		if l = strings.TrimSpace(l); l != "" {
			s = l
			break
		}
	}
	s = strings.TrimSpace(s)
	if len(s) >= 6 && strings.EqualFold(s[:6], "title:") {
		s = s[6:]
	}
	for prev := ""; prev != s; {
		prev = s
		s = strings.TrimSpace(strings.TrimSuffix(strings.Trim(strings.TrimSpace(s), "\"'`*#“”‘’"), "."))
	}
	if strings.ContainsAny(s, "\n\r") {
		return ""
	}
	return clip(oneLine(s), autoTitleChars)
}

// titlePrompt asks for the title. The latest prompt gets a line of its own:
// in a plain list Haiku kept naming the session after an older one. The cube
// shows 16 characters of a name, so that is what it asks for.
func titlePrompt(prompts []string) string {
	var b strings.Builder
	b.WriteString("Name a coding session after what it is working on now.\n\n")
	if n := len(prompts) - 1; n > 0 {
		b.WriteString("Earlier requests, oldest first (context only):\n")
		for _, p := range prompts[:n] {
			b.WriteString("- " + p + "\n")
		}
		b.WriteString("\n")
	}
	b.WriteString("Latest request:\n" + prompts[len(prompts)-1] + "\n\n")
	b.WriteString(`The title names the latest request's work; use an earlier request only when the latest one ` +
		`is too short to say anything ("go", "yes", "continue"). Write 2 or 3 words, at most 16 characters ` +
		`in all. Reply with the title only: no quotes, no full stop.`)
	return b.String()
}

// ClaudeTitler asks Claude Code itself (`claude -p` on Haiku, through the
// user's own login): no tools, no settings files, every hook off (ours would
// report the run as a session), nothing saved to disk. claude finds the
// binary ("" = not found yet).
func ClaudeTitler(run Runner, claude func(context.Context) string) Titler {
	if run == nil {
		run = execRunner
	}
	return func(ctx context.Context, prompts []string) (string, error) {
		bin := claude(ctx)
		if bin == "" {
			return "", errors.New("no claude binary found")
		}
		out, err := run(ctx, bin, "-p", "--model", "haiku", "--no-session-persistence", "--tools", "",
			"--setting-sources", "", "--settings", `{"disableAllHooks":true}`, titlePrompt(prompts))
		return string(out), err
	}
}
