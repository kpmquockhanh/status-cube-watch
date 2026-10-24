// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/claude/claude_test.go and sessions_test.go (transcript cases).

package agents

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

var t0 = time.Date(2026, 10, 24, 12, 0, 0, 0, time.UTC)

func appendTo(t *testing.T, path, s string) {
	t.Helper()
	f, err := os.OpenFile(path, os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0o600)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	if _, err := f.WriteString(s); err != nil {
		t.Fatal(err)
	}
}

func TestTailContextAndModel(t *testing.T) {
	path := filepath.Join(t.TempDir(), "s.jsonl")
	msg := `{"type":"assistant","message":{"id":"m1","model":"claude-opus-5-5","usage":{"input_tokens":10,"output_tokens":100,"cache_read_input_tokens":400000,"cache_creation_input_tokens":100}}}`
	appendTo(t, path, `{"type":"user","gitBranch":"main"}`+"\n"+msg+"\n"+strings.Replace(msg, "m1", "m2", 1)+"\n"+`{"type":"ai-title","aiTitle":"Fix the bug"}`+"\n")
	e := &entry{Transcript: path}
	e.Folder = "api"
	if !tail(e, t0) {
		t.Fatal("expected change")
	}
	if e.Context.Used != 400_110 || e.Context.Window != 1_000_000 {
		t.Errorf("context %+v", e.Context)
	}
	if e.ModelLabel != "opus 5.5" || e.Branch != "main" || e.AITitle != "Fix the bug" || e.Title != "Fix the bug" {
		t.Errorf("model %q branch %q ai %q title %q", e.ModelLabel, e.Branch, e.AITitle, e.Title)
	}
	if tail(e, t0) {
		t.Error("no new bytes: no change")
	}
}

func TestTailTitles(t *testing.T) {
	path := filepath.Join(t.TempDir(), "s.jsonl")
	appendTo(t, path, `{"type":"custom-title","customTitle":"api"}`+"\n"+`{"type":"ai-title","aiTitle":"Fix login race"}`+"\n")
	e := &entry{Transcript: path}
	e.Folder, e.Title = "api", "api"
	tail(e, t0)
	if e.Title != "Fix login race" {
		t.Fatalf("custom title equal to the folder must lose: %q", e.Title)
	}
	appendTo(t, path, `{"type":"custom-title","customTitle":"auth rewrite"}`+"\n")
	tail(e, t0)
	if e.Title != "auth rewrite" {
		t.Fatalf("rename not picked up: %q", e.Title)
	}
}

func TestTailAPIError(t *testing.T) {
	path := filepath.Join(t.TempDir(), "s.jsonl")
	e := &entry{Transcript: path}
	e.SetStatus(Running, t0)
	appendTo(t, path, `{"type":"assistant","isApiErrorMessage":true,"message":{"id":"x","model":"<synthetic>","content":[{"type":"text","text":"API Error: Overloaded"}]}}`+"\n")
	tail(e, t0)
	if e.Status != Error || e.ErrorText != "overloaded" {
		t.Fatalf("api error not surfaced: %s %q", e.Status, e.ErrorText)
	}
	appendTo(t, path, `{"type":"assistant","message":{"id":"m3","model":"claude-opus-5-5","usage":{"input_tokens":1}}}`+"\n")
	tail(e, t0.Add(time.Second))
	if e.Status != Running || e.ErrorText != "" {
		t.Fatalf("recovery: %s %q", e.Status, e.ErrorText)
	}
}

func TestTailPartialAndHugeLines(t *testing.T) {
	path := filepath.Join(t.TempDir(), "s.jsonl")
	huge := `{"type":"user","message":{"content":"` + strings.Repeat("x", 3<<20) + `"}}` + "\n"
	part := `{"type":"assistant","message":{"id":"m1","model":"claude-opus-5-5","usage":{"input_tokens":7}`
	appendTo(t, path, huge+`{"type":"user","gitBranch":"dev"}`+"\n"+part)
	e := &entry{Transcript: path}
	if !tail(e, t0) {
		t.Fatal("expected change")
	}
	if e.Branch != "dev" || e.Context.Used != 0 {
		t.Fatalf("only complete lines count: %+v", e.Agent)
	}
	if want := int64(len(huge)+len(`{"type":"user","gitBranch":"dev"}`)+1) + int64(len(part)); e.Offset != want || e.Pending != part {
		t.Fatalf("offset %d (want %d), pending %d bytes", e.Offset, want, len(e.Pending))
	}
	appendTo(t, path, "}}\n")
	if !tail(e, t0) || e.Context.Used != 7 || e.Pending != "" {
		t.Fatalf("resumed partial line: ctx %d pending %q", e.Context.Used, e.Pending)
	}
}

func TestTailTruncated(t *testing.T) {
	path := filepath.Join(t.TempDir(), "s.jsonl")
	appendTo(t, path, `{"type":"user","gitBranch":"old-long-branch-name"}`+"\n")
	e := &entry{Transcript: path}
	tail(e, t0)
	if err := os.WriteFile(path, []byte(`{"type":"user","gitBranch":"new"}`+"\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	if !tail(e, t0) || e.Branch != "new" {
		t.Fatalf("re-read from 0: %q", e.Branch)
	}
}

func TestTailCatchesUpInOneCall(t *testing.T) {
	path := filepath.Join(t.TempDir(), "s.jsonl")
	msg := func(id string, ctx string) string {
		return `{"type":"assistant","message":{"id":"` + id + `","model":"claude-opus-5-5","usage":{"input_tokens":` + ctx + `}}}` + "\n"
	}
	var b strings.Builder
	b.WriteString(msg("early", "900000"))
	pad := `{"type":"user","message":{"content":"` + strings.Repeat("x", 4000) + `"}}` + "\n"
	for b.Len() < 3*maxRead {
		b.WriteString(pad)
	}
	b.WriteString(msg("late", "50000"))
	appendTo(t, path, b.String())
	e := &entry{Transcript: path}
	if !tail(e, t0) {
		t.Fatal("no change")
	}
	if e.Context.Used != 50_000 || e.Offset != int64(b.Len()) {
		t.Fatalf("one call should reach the end: ctx %d, offset %d of %d", e.Context.Used, e.Offset, b.Len())
	}
}

func TestTailCompactBoundary(t *testing.T) {
	path := filepath.Join(t.TempDir(), "s.jsonl")
	appendTo(t, path, `{"type":"system","subtype":"compact_boundary"}`+"\n")
	e := &entry{Transcript: path}
	e.SetStatus(Running, t0)
	e.Activity = &Activity{Tool: CompactTool, Detail: "auto-compacting"}
	tail(e, t0)
	if e.Status != Running || e.Activity != nil {
		t.Fatalf("auto end: %+v %+v", e.Agent, e.Activity)
	}
}

func TestSubagents(t *testing.T) {
	dir := t.TempDir()
	tr := filepath.Join(dir, "0b5c2f4e-1111-4222-8333-944455556666.jsonl")
	now := time.Now()
	if subs := subagents(tr, now, time.Minute, time.Hour); subs != nil {
		t.Fatalf("no subagents folder: %v", subs)
	}
	sub := filepath.Join(strings.TrimSuffix(tr, ".jsonl"), "subagents")
	if err := os.MkdirAll(sub, 0o700); err != nil {
		t.Fatal(err)
	}
	write := func(name, body string) string {
		p := filepath.Join(sub, name)
		if err := os.WriteFile(p, []byte(body), 0o600); err != nil {
			t.Fatal(err)
		}
		return p
	}
	toolCall := `{"type":"assistant","message":{"stop_reason":null,"content":[{"type":"tool_use","name":"Grep"}]}}`
	result := `{"type":"user","message":{"content":[{"type":"tool_result","content":"ok"}]}}`
	final := `{"type":"assistant","message":{"stop_reason":"end_turn","content":[{"type":"text","text":"done"}]}}`
	write("agent-a1.jsonl", toolCall+"\n"+result+"\n"+`{"type":"attachment","attachment":{}}`+"\n")
	write("agent-a1.meta.json", `{"agentType":"Explore","description":"find  the\nauth code"}`)
	write("agent-a2.jsonl", toolCall+"\n"+result+"\n"+final+"\n") // finished
	old := now.Add(-5 * time.Minute)
	inTool := write("agent-a3.jsonl", toolCall+"\n") // quiet, but inside a tool call: working
	quiet := write("agent-a4.jsonl", toolCall+"\n"+result+"\n")
	for _, f := range []string{inTool, quiet} {
		if err := os.Chtimes(f, old, old); err != nil {
			t.Fatal(err)
		}
	}
	subs := subagents(tr, now, time.Minute, time.Hour)
	if len(subs) != 2 {
		t.Fatalf("want a1 and a3 working: %+v", subs)
	}
	if got := subagents(tr, now, time.Minute, 2*time.Minute); len(got) != 1 {
		t.Fatalf("a tool call past toolWindow is over: %+v", got)
	}
	var a1 Subagent
	for _, x := range subs {
		if x.ID == "a1" {
			a1 = x
		}
	}
	if a1.Title != "find the auth code" || a1.Type != "Explore" || a1.Tool != "Grep" {
		t.Fatalf("a1: %+v", a1)
	}
	// The injected clock decides, not the wall clock: a day later nothing is working.
	if got := subagents(tr, now.Add(24*time.Hour), time.Minute, time.Hour); got != nil {
		t.Fatalf("far future: %+v", got)
	}
}

func TestTranscriptTurn(t *testing.T) {
	path := filepath.Join(t.TempDir(), "s.jsonl")
	call := `{"type":"assistant","message":{"content":[{"type":"tool_use","name":"Bash"}]}}`
	final := `{"type":"assistant","message":{"stop_reason":"end_turn","content":[{"type":"text","text":"ok"}]}}`
	result := `{"type":"user","message":{"content":[{"type":"tool_result"}]}}`
	now := time.Now()
	if running, _ := transcriptTurn(path, now, time.Minute); running {
		t.Fatal("missing file")
	}
	appendTo(t, path, call+"\n")
	old := now.Add(-time.Hour)
	os.Chtimes(path, old, old)
	if running, tool := transcriptTurn(path, now, time.Minute); !running || tool != "Bash" {
		t.Fatalf("tool call out: %v %q", running, tool)
	}
	os.WriteFile(path, []byte(call+"\n"+result+"\n"), 0o600)
	os.Chtimes(path, old, old)
	if running, _ := transcriptTurn(path, now, time.Minute); running {
		t.Fatal("quiet after a result")
	}
	if running, _ := transcriptTurn(path, old.Add(10*time.Second), time.Minute); !running {
		t.Fatal("recent write")
	}
	os.WriteFile(path, []byte(call+"\n"+final+"\n"), 0o600)
	if running, _ := transcriptTurn(path, time.Now(), time.Minute); running {
		t.Fatal("final reply")
	}
}
