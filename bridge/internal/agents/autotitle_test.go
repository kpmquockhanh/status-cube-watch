package agents

import (
	"context"
	"encoding/json"
	"errors"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestTypedPrompt(t *testing.T) {
	for _, c := range []struct{ in, want string }{
		{`"flash the cube"`, "flash the cube"},
		{`"  two\n  lines "`, "two lines"},
		{`"<command-message>review</command-message>\n<command-name>/review</command-name>\n<command-args>the parser</command-args>"`, "/review the parser"},
		{`"<command-name>/clear</command-name>\n<command-args></command-args>"`, "/clear"},
		{`"<task-notification>event</task-notification>"`, ""},
		{`"<local-command-stdout>ok</local-command-stdout>"`, ""},
		{`"[Request interrupted by user]"`, ""},
		{`[{"type":"tool_result","content":"ok"}]`, ""},
		{`[{"type":"image"},{"type":"text","text":"what is this"}]`, "what is this"},
		{`42`, ""},
	} {
		if got := typedPrompt(json.RawMessage(c.in)); got != c.want {
			t.Errorf("typedPrompt(%s) = %q, want %q", c.in, got, c.want)
		}
	}
	if got := typedPrompt(json.RawMessage(`"` + strings.Repeat("a", 400) + `"`)); len([]rune(got)) != 300 {
		t.Errorf("a long prompt is clipped to 300 runes, got %d", len([]rune(got)))
	}
}

func TestAppendRecent(t *testing.T) {
	var r []string
	for i := range 8 {
		r = appendRecent(r, string(rune('a'+i)))
	}
	if strings.Join(r, "") != "cdefgh" {
		t.Fatalf("keeps the last %d: %q", recentMax, r)
	}
	old := r
	r2 := appendRecent(old, "i")
	if strings.Join(old, "") != "cdefgh" || strings.Join(r2, "") != "defghi" {
		t.Fatalf("the old slice is left alone: %q %q", old, r2)
	}
}

func TestCleanTitle(t *testing.T) {
	for in, want := range map[string]string{
		"Cube Usage Card Redesign\n":  "Cube Usage Card Redesign",
		"\n  \"Auto titles\".  ":      "Auto titles",
		"Title: Fix the parser":       "Fix the parser",
		"**Hat picker**\nexplanation": "Hat picker",
		"   ":                         "",
		strings.Repeat("word ", 20):   strings.TrimSpace(strings.Repeat("word ", 8)) + "…",
	} {
		if got := cleanTitle(in); got != want {
			t.Errorf("cleanTitle(%q) = %q, want %q", in, got, want)
		}
	}
}

func TestTitlePriority(t *testing.T) {
	e := &entry{}
	e.Folder = "proj"
	e.AITitle = "First prompt"
	if displayTitle(e) != "First prompt" {
		t.Fatal(displayTitle(e))
	}
	e.AutoTitle = "Recent work"
	if displayTitle(e) != "Recent work" {
		t.Fatal("ours beats Claude's first title")
	}
	e.CustomTitle = "proj"
	if displayTitle(e) != "Recent work" {
		t.Fatal("a rename to the folder is no rename")
	}
	e.CustomTitle = "Mine"
	if displayTitle(e) != "Mine" {
		t.Fatal("a rename wins")
	}
}

// titledTracker is a test tracker with a titler that answers from title (and
// records what it was asked), run inline.
func titledTracker(t *testing.T, title func([]string) (string, error)) (*Tracker, *testEnv, *[][]string) {
	tr, env := newTestTracker(t)
	var asked [][]string
	tr.d.Title = func(_ context.Context, p []string) (string, error) {
		asked = append(asked, p)
		return title(p)
	}
	tr.d.Spawn = func(f func()) { f() }
	return tr, env, &asked
}

func TestAutoTitle(t *testing.T) {
	tr, env, asked := titledTracker(t, func(p []string) (string, error) { return "Working on " + p[len(p)-1], nil })
	tp := filepath.Join(env.projects, "-work-proj", sidA+".jsonl")
	writeLines(t, tp,
		`{"type":"ai-title","aiTitle":"Cube flashing"}`,
		`{"type":"user","message":{"content":"flash the cube"}}`)
	discovered(t, tr, env, tp)
	if a := only(t, tr); a.Title != "Cube flashing" || len(*asked) != 0 {
		t.Fatalf("one prompt keeps Claude's title: %q, asked %v", a.Title, *asked)
	}

	writeLines(t, tp,
		`{"type":"user","isMeta":true,"message":{"content":[{"type":"text","text":"skill body"}]}}`,
		`{"type":"user","message":{"content":[{"type":"tool_result","content":"ok"}]}}`,
		`{"type":"user","message":{"content":"titles"}}`)
	tr.Sync(context.Background())
	if a := only(t, tr); a.Title != "Working on titles" {
		t.Fatalf("second prompt: %q", a.Title)
	}
	if len(*asked) != 1 || strings.Join((*asked)[0], "|") != "flash the cube|titles" {
		t.Fatalf("asked %q", *asked)
	}

	// Nothing new: no request. Something new, too soon: none either.
	tr.Sync(context.Background())
	writeLines(t, tp, `{"type":"user","message":{"content":"tiles"}}`)
	env.advance(time.Minute)
	tr.Sync(context.Background())
	if len(*asked) != 1 || only(t, tr).Title != "Working on titles" {
		t.Fatalf("too soon: asked %q", *asked)
	}
	env.advance(autoTitleEvery)
	tr.Sync(context.Background())
	if len(*asked) != 2 || only(t, tr).Title != "Working on tiles" {
		t.Fatalf("after the wait: asked %q, title %q", *asked, only(t, tr).Title)
	}

	// A rename wins, and stops the requests.
	writeLines(t, tp, `{"type":"custom-title","customTitle":"Mine"}`, `{"type":"user","message":{"content":"more"}}`)
	env.advance(autoTitleEvery)
	tr.Sync(context.Background())
	if len(*asked) != 2 || only(t, tr).Title != "Mine" {
		t.Fatalf("renamed: asked %q, title %q", *asked, only(t, tr).Title)
	}
}

func TestAutoTitleFailureRetries(t *testing.T) {
	fail := true
	tr, env, asked := titledTracker(t, func([]string) (string, error) {
		if fail {
			return "", errors.New("offline")
		}
		return "\n", nil // blank: as bad as an error
	})
	var logged []string
	tr.d.Log = func(m string) { logged = append(logged, m) }
	tp := filepath.Join(env.projects, "-work-proj", sidA+".jsonl")
	writeLines(t, tp, `{"type":"user","message":{"content":"a"}}`, `{"type":"user","message":{"content":"b"}}`)
	discovered(t, tr, env, tp)
	tr.Sync(context.Background())
	if len(*asked) != 1 || only(t, tr).Title != "proj" || len(logged) != 1 || !strings.Contains(logged[0], "offline") {
		t.Fatalf("a failure waits: asked %d, title %q, log %q", len(*asked), only(t, tr).Title, logged)
	}
	fail = false
	env.advance(autoTitleEvery)
	tr.Sync(context.Background())
	if len(*asked) != 2 || only(t, tr).Title != "proj" || len(logged) != 2 {
		t.Fatalf("a blank title is a failure: asked %d, title %q, log %q", len(*asked), only(t, tr).Title, logged)
	}
	if tr.titling {
		t.Fatal("the request is over")
	}
}

func TestAutoTitleOneAtATime(t *testing.T) {
	tr, env := newTestTracker(t)
	var pending []func()
	tr.d.Spawn = func(f func()) { pending = append(pending, f) }
	tr.d.Title = func(context.Context, []string) (string, error) { return "T", nil }
	tp := filepath.Join(env.projects, "-work-proj", sidA+".jsonl")
	writeLines(t, tp, `{"type":"user","message":{"content":"a"}}`, `{"type":"user","message":{"content":"b"}}`)
	discovered(t, tr, env, tp)
	env.advance(autoTitleEvery)
	tr.Sync(context.Background())
	if len(pending) != 1 {
		t.Fatalf("one request out, got %d", len(pending))
	}
	pending[0]()
	if only(t, tr).Title != "T" {
		t.Fatal(only(t, tr).Title)
	}
}

func TestClaudeTitlerArgs(t *testing.T) {
	var got []string
	run := func(_ context.Context, name string, args ...string) ([]byte, error) {
		got = append([]string{name}, args...)
		return []byte("Some Title\n"), nil
	}
	ti := ClaudeTitler(run, func(context.Context) string { return "/bin/claude" })
	out, err := ti(context.Background(), []string{"one", "two"})
	if err != nil || out != "Some Title\n" {
		t.Fatal(out, err)
	}
	joined := strings.Join(got, " ")
	for _, want := range []string{"/bin/claude -p ", "--model haiku", "--no-session-persistence", `{"disableAllHooks":true}`, "- one\n", "Latest request:\ntwo\n"} {
		if !strings.Contains(joined, want) {
			t.Errorf("args lack %q: %q", want, joined)
		}
	}
	if isInteractiveClaude(got) {
		t.Error("discovery must not take the titler's run for a session")
	}
	if _, err := ClaudeTitler(run, func(context.Context) string { return "" })(context.Background(), []string{"x"}); err == nil {
		t.Error("no claude is an error")
	}
}
