// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/claude/claude_test.go (pane cases).

package agents

import (
	"strings"
	"testing"
)

// Captured from Claude Code 2.1.281 (paths shortened).
const permissionPane = `⏺ Bash(date > now.txt)
  ⎿  Waiting…
────────────────────────────────────────────────────────────
 Bash command
   date > now.txt
   Write current date to now.txt
 Do you want to proceed?
 ❯ 1. Yes
   2. Yes, and always allow access to /tmp/at-proj from this project
   3. No
 Esc to cancel · Tab to amend`

const questionPane = `────────────────────────────────────────────────────────────
 ☐ Color
Which color do you prefer?
❯ 1. Red
     A warm, vibrant color
  2. Green
     A natural, calming color
  3. Blue
     A cool, peaceful color
  4. Type something.
────────────────────────────────────────────────────────────
  5. Chat about this
Enter to select · ↑/↓ to navigate · Esc to cancel`

const trustPane = `────────────────────────────────────────────────────────────
 Accessing workspace:
 /tmp/at-proj
 Quick safety check: Is this a project you created or one you trust?
 Claude Code'll be able to read, edit, and execute files here.
 Security guide
 ❯ No, exit
   Yes, I trust this folder
 Enter to confirm · Esc to cancel`

func TestParseOptionsPermission(t *testing.T) {
	got := ParseOptions(permissionPane)
	want := []string{"yes", "yes, and always allow access to /tmp/at-proj from this project", "no"}
	if len(got) != len(want) {
		t.Fatalf("got %d options: %+v", len(got), got)
	}
	for i, w := range want {
		if got[i].Label != w || got[i].Key != string(rune('1'+i)) {
			t.Errorf("option %d = %+v, want %q", i, got[i], w)
		}
	}
	if !got[0].Primary {
		t.Error("first option should be primary")
	}
}

func TestParseOptionsIgnoresEarlierLists(t *testing.T) {
	pane := "Plan:\n 1. read files\n 2. edit\n 3. test\n" + permissionPane
	if got := ParseOptions(pane); len(got) != 3 || got[2].Label != "no" {
		t.Fatalf("want the dialog's options, got %+v", got)
	}
}

func TestParseOptionsNone(t *testing.T) {
	if got := ParseOptions("just some output\n❯ \n"); got != nil {
		t.Fatalf("want nil, got %+v", got)
	}
}

func TestRefineKeepsQuestionLabels(t *testing.T) {
	p := &Prompt{Kind: "question", Options: []Option{{Key: "1", Label: "red — warm"}, {Key: "2", Label: "green"}, {Key: "3", Label: "blue"}}}
	if RefineOptions(p, questionPane) || len(p.Options) != 3 || p.Options[0].Label != "red — warm" {
		t.Fatalf("question options must not change: %+v", p.Options)
	}
}

func TestRefinePermission(t *testing.T) {
	p := &Prompt{Kind: "permission", Options: []Option{
		{Key: "1", Label: "yes", Primary: true},
		{Key: "2", Label: "yes, and don't ask again"},
		{Key: "3", Label: "no"},
	}}
	if !RefineOptions(p, permissionPane) {
		t.Fatal("expected refinement")
	}
	if !strings.Contains(p.Options[1].Label, "always allow") {
		t.Fatalf("got %+v", p.Options)
	}
	if RefineOptions(p, permissionPane) {
		t.Fatal("second refinement with the same pane should report no change")
	}
}

func TestParseCursorMenuTrust(t *testing.T) {
	title, opts, ok := ParseCursorMenu(trustPane)
	if !ok {
		t.Fatal("trust menu not found")
	}
	if title != "trust this folder?" {
		t.Errorf("title = %q", title)
	}
	if len(opts) != 2 || opts[0].Label != "no, exit" || opts[1].Label != "yes, I trust this folder" {
		t.Fatalf("opts = %+v", opts)
	}
	if opts[0].Primary || !opts[1].Primary {
		t.Error("the yes option should be primary")
	}
}

func TestParseCursorMenuRejectsNumbered(t *testing.T) {
	if _, _, ok := ParseCursorMenu(permissionPane); ok {
		t.Fatal("numbered dialog is not a cursor menu")
	}
	if _, _, ok := ParseCursorMenu("❯ \n"); ok {
		t.Fatal("prompt line is not a menu")
	}
}
