// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/claude/hooks.go.

package agents

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// Event is the subset of a Claude Code hook payload we use.
type Event struct {
	Name             string          `json:"hook_event_name"`
	SessionID        string          `json:"session_id"`
	TranscriptPath   string          `json:"transcript_path"`
	Cwd              string          `json:"cwd"`
	Prompt           string          `json:"prompt"`
	ToolName         string          `json:"tool_name"`
	ToolInput        json.RawMessage `json:"tool_input"`
	Message          string          `json:"message"`
	NotificationType string          `json:"notification_type"`
	Source           string          `json:"source"`
	Trigger          string          `json:"trigger"` // PreCompact: manual | auto
}

// apply folds one hook event into the agent. It returns whether the agent
// changed and whether the pane should be scanned for prompt options. The
// caller has already checked ev.TranscriptPath (blank when not trusted).
func apply(e *entry, ev Event, now time.Time) (changed, scanPane bool) {
	e.Background = false // hooks are the real status again
	// Only the main transcript counts; a subagent's own file would swap the
	// context ring to the subagent's and force a full re-read on the way back.
	if ev.TranscriptPath != "" && e.Transcript != ev.TranscriptPath && !strings.Contains(ev.TranscriptPath, "/subagents/") {
		e.Transcript, e.Offset, e.Pending = ev.TranscriptPath, 0, ""
	}
	if ev.SessionID != "" {
		e.SessionID = ev.SessionID
	}
	if ev.Cwd != "" && e.Cwd == "" {
		e.Cwd, e.Folder = ev.Cwd, filepath.Base(ev.Cwd)
	}
	switch ev.Name {
	case "PreCompact":
		e.SetStatus(Running, now)
		detail := "compacting conversation"
		if ev.Trigger == "auto" {
			detail = "auto-compacting · context full"
		}
		e.Activity = &Activity{Tool: CompactTool, Detail: detail}
		e.CompactManual = ev.Trigger == "manual"
	case "SessionStart":
		if ev.Source == "compact" {
			if !endCompact(e, now) {
				return false, false
			}
			break
		}
		if e.Status == Starting || e.Status == Exited || e.Status == Error {
			e.SetStatus(Idle, now)
		}
	case "UserPromptSubmit":
		e.Unseen = false // it's being fed
		e.SetStatus(Running, now)
		e.LastPrompt = clip(oneLine(ev.Prompt), 280)
	case "PreToolUse":
		if ev.ToolName == "AskUserQuestion" {
			if p := questionPrompt(e, ev.ToolInput); p != nil {
				e.SetStatus(Waiting, now)
				e.Waiting = p
				return true, true
			}
		}
		e.SetStatus(Running, now)
		e.Activity = &Activity{Tool: ev.ToolName, Detail: ToolDetail(ev.ToolName, ev.ToolInput, e.Cwd)}
	case "PermissionRequest":
		if ev.ToolName == "AskUserQuestion" {
			// Questions also pass through the permission hook; the question prompt
			// set by PreToolUse is the one the user actually sees.
			if e.Waiting != nil && e.Waiting.Kind != "permission" {
				return false, false
			}
			if p := questionPrompt(e, ev.ToolInput); p != nil {
				e.SetStatus(Waiting, now)
				e.Waiting = p
				return true, true
			}
		}
		e.SetStatus(Waiting, now)
		e.Waiting = permissionPrompt(e, ev.ToolName, ev.ToolInput)
		return true, true
	case "Notification":
		switch {
		case ev.NotificationType == "permission_prompt" || strings.Contains(ev.Message, "permission"):
			if e.Status != Waiting {
				e.SetStatus(Waiting, now)
				e.Waiting = &Prompt{ID: e.NextPromptID(), Kind: "permission",
					Title: lower(ev.Message), Context: ShortPath(e.Cwd), Options: DefaultPermissionOptions()}
			}
			return true, true
		case ev.NotificationType == "idle_prompt":
			if e.Status == Running {
				e.SetStatus(Idle, now)
			}
		case ev.NotificationType == "elicitation_dialog":
			e.SetStatus(Waiting, now)
			e.Waiting = &Prompt{ID: e.NextPromptID(), Kind: "input", Title: lower(ev.Message)}
			return true, true
		default:
			return false, false
		}
	case "PostToolUse":
		e.SetStatus(Running, now)
		e.Activity = nil
	case "Stop":
		e.SetStatus(Idle, now)
		e.Unseen = true // finished: hungry until it is fed again
	case "SessionEnd":
		e.SetStatus(Exited, now)
		e.Unseen = false
	default:
		return false, false
	}
	return true, false
}

// DefaultPermissionOptions mirror Claude Code's dialog when the pane can't be read.
func DefaultPermissionOptions() []Option {
	return []Option{
		{Key: "1", Label: "yes", Primary: true},
		{Key: "2", Label: "yes, and don't ask again"},
		{Key: "3", Label: "no"},
	}
}

func permissionPrompt(e *entry, tool string, input json.RawMessage) *Prompt {
	title, ctx := "allow "+strings.ToLower(tool)+"?", strings.ToLower(tool)
	detail := ToolDetail(tool, input, e.Cwd)
	switch tool {
	case "Bash":
		title = "run this command?"
		detail = field(input, "command") // full command, not the one-line summary
	case "Edit", "MultiEdit", "NotebookEdit":
		title = "edit this file?"
	case "Write":
		title = "create this file?"
	case "Read":
		title = "read this file?"
	case "WebFetch":
		title = "fetch this url?"
	case "WebSearch":
		title = "search the web?"
	}
	if strings.HasPrefix(tool, "mcp__") {
		parts := strings.Split(tool, "__")
		title, ctx = "use "+parts[len(parts)-1]+"?", "mcp · "+parts[1]
	}
	if e.Cwd != "" {
		ctx += " · " + ShortPath(e.Cwd)
	}
	return &Prompt{ID: e.NextPromptID(), Kind: "permission", Title: title, Detail: clip(detail, 2000),
		Context: ctx, Options: DefaultPermissionOptions()}
}

func questionPrompt(e *entry, input json.RawMessage) *Prompt {
	var in struct {
		Questions []struct {
			Question string `json:"question"`
			Header   string `json:"header"`
			Options  []struct {
				Label       string `json:"label"`
				Description string `json:"description"`
			} `json:"options"`
		} `json:"questions"`
	}
	if json.Unmarshal(input, &in) != nil || len(in.Questions) == 0 {
		return nil
	}
	q := in.Questions[0]
	p := &Prompt{ID: e.NextPromptID(), Kind: "question", Title: lower(q.Question),
		Context: fmt.Sprintf("question · 1 of %d", len(in.Questions))}
	for i, o := range q.Options {
		label := o.Label
		if o.Description != "" {
			label += " — " + o.Description
		}
		p.Options = append(p.Options, Option{Key: fmt.Sprint(i + 1), Label: label, Primary: i == 0})
	}
	if len(in.Questions) > 1 || len(p.Options) == 0 {
		p.Kind = "input" // multi-question forms are answered at the terminal
		p.Options = nil
	}
	return p
}

// ToolDetail is a one-line summary of what a tool call touches.
func ToolDetail(tool string, input json.RawMessage, cwd string) string {
	rel := func(p string) string {
		if cwd != "" {
			if r, err := filepath.Rel(cwd, p); err == nil && !strings.HasPrefix(r, "..") {
				return r
			}
		}
		return ShortPath(p)
	}
	switch tool {
	case "Bash":
		return clip(oneLine(field(input, "command")), 160)
	case "Edit", "MultiEdit", "Write", "Read", "NotebookEdit":
		if p := field(input, "file_path"); p != "" {
			return rel(p)
		}
		return rel(field(input, "notebook_path"))
	case "Grep", "Glob":
		return clip(field(input, "pattern"), 120)
	case "WebFetch":
		return clip(field(input, "url"), 160)
	case "WebSearch":
		return clip(field(input, "query"), 120)
	case "Task", "Agent":
		return clip(field(input, "description"), 120)
	case "TodoWrite":
		var in struct {
			Todos []json.RawMessage `json:"todos"`
		}
		_ = json.Unmarshal(input, &in)
		return fmt.Sprintf("planning %d steps", len(in.Todos))
	}
	return ""
}

func field(raw json.RawMessage, key string) string {
	var m map[string]any
	if json.Unmarshal(raw, &m) != nil {
		return ""
	}
	if s, ok := m[key].(string); ok {
		return s
	}
	return ""
}

// ShortPath renders a path with ~ for the home directory.
func ShortPath(p string) string {
	home, _ := os.UserHomeDir()
	if home != "" && (p == home || strings.HasPrefix(p, home+"/")) {
		return "~" + strings.TrimPrefix(p, home)
	}
	return p
}
