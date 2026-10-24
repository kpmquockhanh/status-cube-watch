// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/model/model.go (trimmed to what the bridge uses).

// Package agents tracks Claude Code sessions for the Agents card.
package agents

import "time"

type Status string

const (
	Starting Status = "starting"
	Running  Status = "running"
	Waiting  Status = "waiting"
	Idle     Status = "idle"
	Error    Status = "error"
	Exited   Status = "exited"
)

// Agent is one tracked Claude Code session.
type Agent struct {
	ID          string
	SessionID   string
	Folder      string
	Cwd         string
	Branch      string
	Model       string
	ModelLabel  string
	Title       string
	Status      Status
	StatusSince time.Time
	Activity    *Activity // nil unless running
	Waiting     *Prompt   // nil unless waiting
	ErrorText   string
	LastPrompt  string
	Context     struct{ Used, Window int64 }
	Subagents   []Subagent
	Unseen      bool   // finished a turn nobody looked at
	Pane        string // tmux pane id ("%12"), "" = not in tmux
}

type Activity struct{ Tool, Detail string }

// Prompt is a blocking question; Kind is permission, question or input.
type Prompt struct {
	ID, Kind, Title, Detail, Context string
	Options                          []Option
}

type Option struct {
	Key, Label string
	Primary    bool
}

// Subagent is one of an agent's subagents that is still working.
type Subagent struct{ ID, Title, Type, Tool string }
