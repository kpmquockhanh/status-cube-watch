package cards

import (
	"bytes"
	"encoding/json"
	"math"
	"sort"
	"strconv"
	"time"

	"claude-status-cube/bridge/internal/agents"
)

// MaxPayloadBytes caps every /api/status body, either variant: the Mac helper
// drops anything larger (CubeProtocol.maxPayload). Build trims to it.
const MaxPayloadBytes = 2048

// MaxTiles is the most agents the Agents card lists.
const MaxTiles = 8

// Agents card field limits, in printable ASCII characters (a firmware buffer
// needs one more byte for the NUL). Plan 3 sizes payload.h from these.
const (
	TileIDChars       = 15  // Tile.I, AgentPrompt.I
	TileNameChars     = 16  // Tile.N
	TileLineChars     = 16  // Tile.L
	TileKindChars     = 5   // Tile.K: run|wait|idle|err|exit|start
	TilePoseChars     = 8   // Tile.P: the longest is "delegate"
	PromptIDChars     = 15  // AgentPrompt.P
	PromptKindChars   = 10  // AgentPrompt.K: permission|question|input
	PromptTitleChars  = 40  // AgentPrompt.T
	PromptDetailChars = 120 // AgentPrompt.D
	PromptCtxChars    = 40  // AgentPrompt.C
	PromptOptions     = 6   // len(AgentPrompt.O)
	OptionKeyChars    = 7   // PromptOption.K
	OptionLabelChars  = 40  // PromptOption.L
	PromptWhyChars    = 7   // AgentPrompt.X: "tmux" or ""
)

// fitN keeps at most n characters (Fit's bytes include the NUL).
func fitN(s string, n int) string { return Fit(s, n+1) }

// Tile is one agent on the Agents card. It never carries a tool's detail
// (paths, commands, URLs) or a prompt's text.
type Tile struct {
	I string `json:"i"`           // agent id
	N string `json:"n"`           // folder name
	L string `json:"l"`           // status line: "needs you", "Edit +3", "thinking", "idle 12m", ...
	K string `json:"k"`           // run|wait|idle|err|exit|start
	P string `json:"p"`           // pose: idle|start|run|think|read|write|bash|web|plan|delegate|compact|wait|err|exit|hungry
	X int    `json:"x"`           // context used, 0..100
	U int    `json:"u,omitempty"` // 1 = finished a turn nobody looked at
}

// AgentPrompt is the longest-waiting agent's question; only in the loopback
// (Full) payload.
type AgentPrompt struct {
	I string         `json:"i"`           // agent id
	P string         `json:"p"`           // prompt id
	K string         `json:"k"`           // permission|question|input
	T string         `json:"t"`           // title
	D string         `json:"d"`           // detail
	C string         `json:"c"`           // context
	O []PromptOption `json:"o,omitempty"` // answer buttons
	X string         `json:"x,omitempty"` // why buttons are off: "tmux" (not in a tmux pane); "" = answerable
}

// PromptOption is one answer button: the key typed into the pane and its label.
type PromptOption struct {
	K string `json:"k"`
	L string `json:"l"`
}

// Marshal encodes a payload exactly as /api/status serves it: no HTML
// escapes (they cost bytes on the BLE link), no trailing newline.
func Marshal(p Payload) ([]byte, error) {
	var buf bytes.Buffer
	enc := json.NewEncoder(&buf)
	enc.SetEscapeHTML(false)
	if err := enc.Encode(p); err != nil {
		return nil, err
	}
	return bytes.TrimSuffix(buf.Bytes(), []byte("\n")), nil
}

// statusRank orders the tiles: waiting, running, starting, error, idle, exited.
var statusRank = map[agents.Status]int{
	agents.Waiting: 0, agents.Running: 1, agents.Starting: 2, agents.Error: 3, agents.Idle: 4, agents.Exited: 5,
}

func rank(s agents.Status) int {
	if r, ok := statusRank[s]; ok {
		return r
	}
	return statusRank[agents.Idle]
}

// sortAgents: by status group; waiting agents longest-waiting first; then
// folder name, then id.
func sortAgents(list []agents.Agent) []agents.Agent {
	out := append([]agents.Agent(nil), list...)
	sort.SliceStable(out, func(i, j int) bool {
		a, b := out[i], out[j]
		if ra, rb := rank(a.Status), rank(b.Status); ra != rb {
			return ra < rb
		}
		if a.Status == agents.Waiting && !a.StatusSince.Equal(b.StatusSince) {
			return a.StatusSince.Before(b.StatusSince)
		}
		if a.Folder != b.Folder {
			return a.Folder < b.Folder
		}
		return a.ID < b.ID
	})
	return out
}

// agentsCardOf builds the Agents card; nil when no agent is open.
func agentsCardOf(list []agents.Agent, full bool, now time.Time) *Card {
	if len(list) == 0 {
		return nil
	}
	sorted := sortAgents(list)
	waiting, running := 0, 0
	for _, a := range sorted {
		switch a.Status {
		case agents.Waiting:
			waiting++
		case agents.Running:
			running++
		}
	}
	c := &Card{T: "AGENTS", V: strconv.Itoa(len(sorted)), S1: "ALL IDLE", C: "ink"}
	switch {
	case waiting == 1:
		c.S1, c.C = "1 NEEDS YOU", "accent"
	case waiting > 1:
		c.S1, c.C = strconv.Itoa(waiting)+" NEED YOU", "accent"
	case running > 0:
		c.S1, c.C = strconv.Itoa(running)+" RUNNING", "blue"
	}
	for _, a := range sorted[:min(len(sorted), MaxTiles)] {
		c.A = append(c.A, tileOf(a, now))
	}
	if full {
		for _, a := range sorted {
			if a.Status == agents.Waiting && a.Waiting != nil {
				c.Q = promptOf(a)
				break
			}
		}
	}
	return c
}

func tileOf(a agents.Agent, now time.Time) Tile {
	t := Tile{I: fitN(a.ID, TileIDChars), N: fitN(a.Folder, TileNameChars), X: contextPct(a.Context.Used, a.Context.Window)}
	if a.Unseen {
		t.U = 1
	}
	switch a.Status {
	case agents.Waiting:
		t.K, t.P, t.L = "wait", "wait", "needs you"
	case agents.Running:
		t.K, t.P, t.L = "run", "think", "thinking"
		if a.Activity != nil && a.Activity.Tool != "" {
			t.P, t.L = toolPose(a.Activity.Tool), a.Activity.Tool
		}
		if n := len(a.Subagents); n > 0 {
			t.L += " +" + strconv.Itoa(n)
		}
	case agents.Starting:
		t.K, t.P, t.L = "start", "start", "starting"
	case agents.Error:
		t.K, t.P, t.L = "err", "err", "error"
	case agents.Exited:
		t.K, t.P, t.L = "exit", "exit", "exited"
	default: // idle
		t.K, t.P, t.L = "idle", "idle", idleLine(a.StatusSince, now)
		if a.Unseen {
			t.P = "hungry"
		}
	}
	t.L = fitN(t.L, TileLineChars)
	return t
}

// toolPose is the pose of a running agent using tool.
func toolPose(tool string) string {
	switch tool {
	case "Read", "Grep", "Glob", "LS":
		return "read"
	case "Edit", "MultiEdit", "Write", "NotebookEdit":
		return "write"
	case "Bash":
		return "bash"
	case "WebFetch", "WebSearch":
		return "web"
	case "TodoWrite", "ExitPlanMode":
		return "plan"
	case "Task", "Agent":
		return "delegate"
	case "Compact":
		return "compact"
	}
	return "run"
}

// idleLine: "idle" under a minute, else one floored unit: "idle 12m",
// "idle 3h", "idle 2d".
func idleLine(since, now time.Time) string {
	if since.IsZero() {
		return "idle"
	}
	d := now.Sub(since)
	switch {
	case d < time.Minute:
		return "idle"
	case d < time.Hour:
		return "idle " + itoa(int64(d/time.Minute)) + "m"
	case d < 24*time.Hour:
		return "idle " + itoa(int64(d/time.Hour)) + "h"
	}
	return "idle " + itoa(int64(d/(24*time.Hour))) + "d"
}

// contextPct is round(100*used/window), clamped to 0..100; 0 without a window.
func contextPct(used, window int64) int {
	if window <= 0 {
		return 0
	}
	return int(min(100, max(0, math.Round(100*float64(used)/float64(window)))))
}

func promptOf(a agents.Agent) *AgentPrompt {
	w := a.Waiting
	q := &AgentPrompt{I: fitN(a.ID, TileIDChars), P: fitN(w.ID, PromptIDChars), K: fitN(w.Kind, PromptKindChars),
		T: fitN(w.Title, PromptTitleChars), D: fitN(w.Detail, PromptDetailChars), C: fitN(w.Context, PromptCtxChars)}
	for _, o := range w.Options[:min(len(w.Options), PromptOptions)] {
		q.O = append(q.O, PromptOption{K: fitN(o.Key, OptionKeyChars), L: fitN(o.Label, OptionLabelChars)})
	}
	if a.Pane == "" {
		q.X = "tmux"
	}
	return q
}

// fitBudget trims p until it marshals to at most budget bytes, one step at a
// time, least valuable first: the prompt's detail, options beyond 4, tiles
// from the end down to 4, the prompt, the extra cards from the end, and as a
// last resort (unreachable with today's field limits) the Agents card.
// agentsAt is the Agents card's index (-1: none); cards from extraFrom on are
// the extra cards.
func fitBudget(p *Payload, agentsAt, extraFrom, budget int) {
	ac := func() *Card {
		if agentsAt < 0 || agentsAt >= len(p.Cards) {
			return nil
		}
		return &p.Cards[agentsAt]
	}
	steps := []func() bool{
		func() bool {
			if c := ac(); c != nil && c.Q != nil && c.Q.D != "" {
				c.Q.D = ""
				return true
			}
			return false
		},
		func() bool {
			if c := ac(); c != nil && c.Q != nil && len(c.Q.O) > 4 {
				c.Q.O = c.Q.O[:4]
				return true
			}
			return false
		},
		func() bool {
			if c := ac(); c != nil && len(c.A) > 4 {
				c.A = c.A[:len(c.A)-1]
				return true
			}
			return false
		},
		func() bool {
			if c := ac(); c != nil && c.Q != nil {
				c.Q = nil
				return true
			}
			return false
		},
		func() bool {
			if len(p.Cards) > extraFrom {
				p.Cards = p.Cards[:len(p.Cards)-1]
				return true
			}
			return false
		},
		func() bool {
			if ac() != nil {
				p.Cards = append(p.Cards[:agentsAt:agentsAt], p.Cards[agentsAt+1:]...)
				agentsAt = -1
				return true
			}
			return false
		},
	}
	for i := 0; i < len(steps) && !fits(*p, budget); {
		if !steps[i]() {
			i++
		}
	}
}

func fits(p Payload, budget int) bool {
	b, err := Marshal(p)
	return err == nil && len(b) <= budget
}
