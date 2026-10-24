// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/claude/claude_test.go (Apply cases).

package agents

import (
	"encoding/json"
	"strings"
	"testing"
	"time"
)

var hookNow = time.Unix(1_800_000_000, 0)

func ev(name string, kv ...string) Event {
	e := Event{Name: name}
	for i := 0; i+1 < len(kv); i += 2 {
		switch kv[i] {
		case "prompt":
			e.Prompt = kv[i+1]
		case "tool":
			e.ToolName = kv[i+1]
		case "input":
			e.ToolInput = json.RawMessage(kv[i+1])
		case "type":
			e.NotificationType = kv[i+1]
		case "message":
			e.Message = kv[i+1]
		}
	}
	return e
}

func TestApplyLifecycle(t *testing.T) {
	e := &entry{}
	e.Status = Starting
	e.Cwd = "/work/proj"

	steps := []struct {
		ev   Event
		want Status
	}{
		{ev("SessionStart"), Idle},
		{ev("UserPromptSubmit", "prompt", "fix   the\nbug"), Running},
		{ev("PreToolUse", "tool", "Edit", "input", `{"file_path":"/work/proj/src/a.go"}`), Running},
		{ev("PermissionRequest", "tool", "Bash", "input", `{"command":"rm -rf build\nmake"}`), Waiting},
		{ev("PostToolUse", "tool", "Bash"), Running},
		{ev("Stop"), Idle},
		{ev("SessionEnd"), Exited},
	}
	for i, s := range steps {
		apply(e, s.ev, hookNow)
		if e.Status != s.want {
			t.Fatalf("step %d (%s): status %s, want %s", i, s.ev.Name, e.Status, s.want)
		}
		switch i {
		case 1:
			if e.LastPrompt != "fix the bug" {
				t.Errorf("prompt %q", e.LastPrompt)
			}
		case 2:
			if e.Activity == nil || e.Activity.Detail != "src/a.go" {
				t.Errorf("activity %+v", e.Activity)
			}
		case 3:
			p := e.Waiting
			if p == nil || p.Kind != "permission" || p.Title != "run this command?" || p.Detail != "rm -rf build\nmake" {
				t.Fatalf("prompt %+v", p)
			}
			if e.Activity != nil {
				t.Error("activity should clear while waiting")
			}
		case 4:
			if e.Waiting != nil {
				t.Error("waiting should clear once the tool ran")
			}
		}
	}
}

func TestApplyQuestionBeatsPermissionHook(t *testing.T) {
	e := &entry{}
	in := `{"questions":[{"question":"Which color?","options":[{"label":"Red","description":"warm"},{"label":"Green"}]}]}`
	apply(e, ev("PreToolUse", "tool", "AskUserQuestion", "input", in), hookNow)
	first := e.Waiting
	if first == nil || first.Kind != "question" || len(first.Options) != 2 || first.Options[0].Label != "Red — warm" {
		t.Fatalf("question %+v", first)
	}
	apply(e, ev("PermissionRequest", "tool", "AskUserQuestion", "input", in), hookNow)
	if e.Waiting != first {
		t.Fatalf("permission hook replaced the question: %+v", e.Waiting)
	}
}

func TestApplyMultiQuestionIsInput(t *testing.T) {
	e := &entry{}
	in := `{"questions":[{"question":"A?","options":[{"label":"x"},{"label":"y"}]},{"question":"B?","options":[{"label":"z"},{"label":"w"}]}]}`
	apply(e, ev("PreToolUse", "tool", "AskUserQuestion", "input", in), hookNow)
	if e.Waiting == nil || e.Waiting.Kind != "input" || len(e.Waiting.Options) != 0 {
		t.Fatalf("multi-question form should be input-only: %+v", e.Waiting)
	}
}

func TestApplyPromptIDsChange(t *testing.T) {
	e := &entry{}
	apply(e, ev("PermissionRequest", "tool", "Bash", "input", `{"command":"a"}`), hookNow)
	id1 := e.Waiting.ID
	apply(e, ev("PostToolUse"), hookNow)
	apply(e, ev("PermissionRequest", "tool", "Bash", "input", `{"command":"b"}`), hookNow)
	if e.Waiting.ID == id1 {
		t.Fatal("a new prompt must get a new id")
	}
}

func TestApplyNotifications(t *testing.T) {
	e := &entry{}
	e.Cwd = "/work/proj"
	if _, scan := apply(e, ev("Notification", "type", "permission_prompt", "message", "Claude needs your permission to use Bash"), hookNow); !scan {
		t.Fatal("a permission notification should scan the pane")
	}
	if e.Status != Waiting || e.Waiting == nil || e.Waiting.Kind != "permission" || len(e.Waiting.Options) != 3 {
		t.Fatalf("permission notification: %+v", e.Waiting)
	}
	apply(e, ev("UserPromptSubmit", "prompt", "go"), hookNow)
	apply(e, ev("Notification", "type", "idle_prompt"), hookNow)
	if e.Status != Idle {
		t.Fatalf("idle prompt: %s", e.Status)
	}
	apply(e, ev("Notification", "type", "elicitation_dialog", "message", "Pick a server"), hookNow)
	if e.Status != Waiting || e.Waiting.Kind != "input" || e.Waiting.Title != "pick a server" {
		t.Fatalf("elicitation: %+v", e.Waiting)
	}
	if changed, _ := apply(e, ev("Notification", "type", "something_else"), hookNow); changed {
		t.Fatal("unknown notification changes nothing")
	}
}

func TestApplyTranscriptPath(t *testing.T) {
	e := &entry{Transcript: "/p/a.jsonl", Offset: 99, Pending: "x"}
	apply(e, Event{Name: "Stop", TranscriptPath: "/p/a/subagents/agent-1.jsonl"}, hookNow)
	if e.Transcript != "/p/a.jsonl" || e.Offset != 99 {
		t.Fatalf("a subagent transcript must not replace the main one: %+v", e)
	}
	apply(e, Event{Name: "Stop", TranscriptPath: "/p/b.jsonl", Cwd: "/work/proj"}, hookNow)
	if e.Transcript != "/p/b.jsonl" || e.Offset != 0 || e.Pending != "" {
		t.Fatalf("a new transcript restarts the tail: %+v", e)
	}
	if e.Cwd != "/work/proj" || e.Folder != "proj" {
		t.Fatalf("cwd and folder learned from the hook: %q %q", e.Cwd, e.Folder)
	}
}

func TestToolDetail(t *testing.T) {
	cases := []struct{ tool, in, want string }{
		{"Bash", `{"command":"go   test\n./..."}`, "go test ./..."},
		{"Read", `{"file_path":"/work/proj/main.go"}`, "main.go"},
		{"Grep", `{"pattern":"TODO"}`, "TODO"},
		{"TodoWrite", `{"todos":[{},{},{}]}`, "planning 3 steps"},
		{"Unknown", `{}`, ""},
	}
	for _, c := range cases {
		if got := ToolDetail(c.tool, json.RawMessage(c.in), "/work/proj"); got != c.want {
			t.Errorf("%s: %q, want %q", c.tool, got, c.want)
		}
	}
}

func TestApplyHungry(t *testing.T) {
	e := &entry{}
	apply(e, ev("UserPromptSubmit", "prompt", "go"), hookNow)
	if e.Unseen {
		t.Fatal("working agent can't be hungry")
	}
	apply(e, ev("Stop"), hookNow)
	if !e.Unseen || e.Status != Idle {
		t.Fatalf("finished turn should be unseen: %+v", e.Agent)
	}
	apply(e, ev("UserPromptSubmit", "prompt", "next"), hookNow)
	if e.Unseen {
		t.Fatal("feeding it a prompt clears hungry")
	}
}

func TestCompaction(t *testing.T) {
	e := &entry{}
	e.SetStatus(Idle, hookNow)
	// A /compact you typed: compacting, then back to idle (not hungry).
	apply(e, Event{Name: "PreCompact", Trigger: "manual"}, hookNow)
	if e.Status != Running || e.Activity == nil || e.Activity.Tool != CompactTool || e.Activity.Detail != "compacting conversation" {
		t.Fatalf("manual start: %+v %+v", e.Agent, e.Activity)
	}
	apply(e, Event{Name: "SessionStart", Source: "compact"}, hookNow)
	if e.Status != Idle || e.Activity != nil || e.Unseen {
		t.Fatalf("manual end: %+v %+v", e.Agent, e.Activity)
	}
	e.SetStatus(Running, hookNow)
	apply(e, Event{Name: "PreCompact", Trigger: "auto"}, hookNow)
	if e.Activity == nil || e.Activity.Detail != "auto-compacting · context full" {
		t.Fatalf("auto start: %+v", e.Activity)
	}
	apply(e, Event{Name: "SessionStart", Source: "compact"}, hookNow)
	if e.Status != Running || e.Activity != nil {
		t.Fatalf("auto end: %+v %+v", e.Agent, e.Activity)
	}
	// A stray compact SessionStart changes nothing.
	if changed, _ := apply(e, Event{Name: "SessionStart", Source: "compact"}, hookNow); changed {
		t.Fatal("no compaction running")
	}
}

// Review Focus 2: a Write of a 3 MB file still produces a prompt, with the
// detail clipped.
func TestHookLargePayload(t *testing.T) {
	content := strings.Repeat("x", 3<<20)
	body, _ := json.Marshal(map[string]any{
		"hook_event_name": "PermissionRequest",
		"session_id":      "0b5c2f4e-1111-4222-8333-944455556666",
		"cwd":             "/work/proj",
		"tool_name":       "Write",
		"tool_input":      map[string]string{"file_path": "/work/proj/" + strings.Repeat("d/", 1500) + "big.txt", "content": content},
	})
	var e Event
	if err := json.Unmarshal(body, &e); err != nil {
		t.Fatal(err)
	}
	x := &entry{}
	x.Cwd = "/work/proj"
	apply(x, e, hookNow)
	if x.Status != Waiting || x.Waiting == nil || x.Waiting.Kind != "permission" || x.Waiting.Title != "create this file?" {
		t.Fatalf("prompt %+v", x.Waiting)
	}
	if n := len([]rune(x.Waiting.Detail)); n > 2000 {
		t.Fatalf("detail is %d runes", n)
	}

	// The same body through the tracker (no size cut on the way in).
	tr, _ := newTestTracker(t)
	if err := tr.Hook(body); err != nil {
		t.Fatal(err)
	}
	a := tr.Snapshot()
	if len(a) != 1 || a[0].Status != Waiting || a[0].Waiting.Title != "create this file?" || len([]rune(a[0].Waiting.Detail)) > 2000 {
		t.Fatalf("tracker: %+v", a)
	}
}
