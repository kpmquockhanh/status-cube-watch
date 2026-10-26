package cards

import (
	"bytes"
	"cmp"
	"encoding/json"
	"math"
	"slices"
	"sort"
	"strconv"
	"strings"
	"time"

	"claude-status-cube/bridge/internal/agents"
)

// MaxPayloadBytes caps every /api/status body, either variant: the Mac helper
// drops anything larger (CubeProtocol.maxPayload). Build trims to it.
const MaxPayloadBytes = 2048

// MaxTiles is the most agents the Agents card lists.
const MaxTiles = 8

// Agents card field limits, in printable ASCII characters (a firmware buffer
// needs one more byte for the NUL). Plan 3 sizes payload.h from these. A
// title or detail longer than its limit is cut to end in "..." within it.
// When the payload budget drops the detail, q.cut is 1; when it drops the
// whole prompt (Full variant only), the card's qc is 1.
const (
	TileIDChars       = 15  // Tile.I, AgentPrompt.I
	TileNameChars     = 16  // Tile.N
	TileLineChars     = 16  // Tile.L, in characters: it may hold the two-byte "\u00b7"
	TileLineBytes     = 23  // Tile.L, in bytes (payload.h: line[24])
	TileSubsBytes     = 7   // Tile.S: "\u00d7" and the count
	TileKindChars     = 5   // Tile.K: run|wait|idle|err|exit|start
	TilePoseChars     = 9   // Tile.P: the longest is "celebrate"
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

// fitMarked is fitN, but a cut string ends in "..." (within the n).
func fitMarked(s string, n int) string {
	all := Fit(s, math.MaxInt)
	if len(all) <= n {
		return all
	}
	return strings.TrimRight(all[:n-3], " ") + "..."
}

// sep joins a tile's status-line parts, as agents-deck does. It is the one
// non-ASCII character a tile line carries; the firmware's rm11 has the glyph.
const sep = " \u00b7 "

// fitLine cuts a status line to TileLineChars characters (it may hold sep),
// ending a cut one in "...".
func fitLine(s string) string {
	r := []rune(s)
	if len(r) <= TileLineChars {
		return s
	}
	return strings.TrimRight(string(r[:TileLineChars-3]), " ") + "..."
}

// Tile is one agent on the Agents card. It never carries a tool's detail
// (paths, commands, URLs) or a prompt's text.
type Tile struct {
	I string  `json:"i"`           // agent id
	N string  `json:"n"`           // folder name
	L string  `json:"l"`           // status line: "allow bash?", "edit · 41% ctx", "hungry · feed me", "done · 12m ago", ...
	K string  `json:"k"`           // run|wait|idle|err|exit|start
	P string  `json:"p"`           // pose: idle|start|run|think|read|write|bash|web|plan|delegate|compact|wait|err|exit|hungry|celebrate
	X int     `json:"x"`           // context used, 0..100
	U int     `json:"u,omitempty"` // 1 = finished a turn nobody looked at
	H *int    `json:"h,omitempty"` // hat: shape*8 + colour (0..103); absent = no hat
	S *string `json:"s,omitempty"` // subagents at work: "" for one, "×N" for more; absent = none
}

// AgentPrompt is the longest-waiting agent's question; only in the loopback
// (Full) payload.
type AgentPrompt struct {
	I string         `json:"i"`           // agent id
	P string         `json:"p"`           // prompt id
	K string         `json:"k"`           // permission|question|input
	T string         `json:"t"`           // title ("..." when cut)
	D string         `json:"d"`           // detail ("..." when cut; "" with Cut when dropped)
	C string         `json:"c"`           // context
	O []PromptOption `json:"o,omitempty"` // answer buttons
	X string         `json:"x,omitempty"` // why buttons are off: "tmux" (not in a tmux pane); "" = answerable
	// Cut is 1 when the payload budget dropped the detail D (D is then "").
	Cut int `json:"cut,omitempty"`
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

// byPriority: by status group; waiting agents longest-waiting first; then
// folder name, then id. It decides which agents get one of the MaxTiles
// places and which prompt is q.
func byPriority(list []agents.Agent) []agents.Agent {
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

// sortAgents is the order the tiles are drawn in: agents in tmux as their
// panes sit on screen (sessions, windows, left to right, top to bottom; see
// agents.Pane), so swapping two panes swaps their tiles; then the others in
// byPriority's order.
func sortAgents(list []agents.Agent) []agents.Agent {
	out := byPriority(list)
	sort.SliceStable(out, func(i, j int) bool {
		a, b := out[i], out[j]
		if (a.Pane != "") != (b.Pane != "") {
			return a.Pane != ""
		}
		return a.Pane != "" && a.PaneOrder < b.PaneOrder
	})
	return out
}

// agentsCardOf builds the Agents card; nil when no agent is open.
func agentsCardOf(list []agents.Agent, full bool, now time.Time, hats func(string) (int, bool)) *Card {
	if len(list) == 0 {
		return nil
	}
	sorted := byPriority(list)
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
	for _, a := range sortAgents(sorted[:min(len(sorted), MaxTiles)]) {
		c.A = append(c.A, tileOf(a, now, hats))
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

func tileOf(a agents.Agent, now time.Time, hats func(string) (int, bool)) Tile {
	t := Tile{I: fitN(a.ID, TileIDChars), N: fitMarked(cmp.Or(a.Title, a.Folder), TileNameChars), X: contextPct(a.Context.Used, a.Context.Window)}
	{
		// A hat picked for this session beats its automatic one; -1 = no hat.
		key := HatKey(a)
		code, ok := -1, false
		if hats != nil {
			code, ok = hats(key)
		}
		if !ok {
			shape, color := HatOf(key)
			code = shape*hatColors + color
		}
		if code >= 0 {
			t.H = &code
		}
	}
	if a.Unseen {
		t.U = 1
	}
	if n := len(a.Subagents); n > 0 {
		s := ""
		if n > 1 {
			s = "\u00d7" + strconv.Itoa(n)
		}
		t.S = &s
	}
	switch a.Status {
	case agents.Waiting:
		t.K, t.P, t.L = "wait", "wait", waitLine(a.Waiting)
	case agents.Running:
		t.K, t.P, t.L = "run", "think", "thinking"
		if a.Activity != nil && a.Activity.Tool != "" {
			t.P = toolPose(a.Activity.Tool)
			t.L = Fit(toolVerb(a.Activity.Tool), math.MaxInt)
		}
		if a.Context.Window > 0 {
			// The full "<verb> · N% ctx" when it fits, else drop the word "ctx".
			if full := t.L + sep + strconv.Itoa(t.X) + "% ctx"; len([]rune(full)) <= TileLineChars {
				t.L = full
			} else {
				t.L += sep + strconv.Itoa(t.X) + "%"
			}
		}
	case agents.Starting:
		t.K, t.P, t.L = "start", "start", "starting..."
	case agents.Error:
		t.K, t.P, t.L = "err", "err", cmp.Or(Fit(a.ErrorText, math.MaxInt), "error")
	case agents.Exited:
		t.K, t.P, t.L = "exit", "exit", "exited"
	default: // idle
		t.K, t.P = "idle", "idle"
		switch {
		case a.Unseen:
			t.P, t.L = "hungry", "hungry"+sep+"feed me"
		case !a.StatusSince.IsZero() && now.Sub(a.StatusSince) < celebrateFor:
			t.P, t.L = "celebrate", "done"+sep+agoOf(a.StatusSince, now)+" ago"
		default:
			t.L = "done" + sep + agoOf(a.StatusSince, now) + " ago"
		}
	}
	t.L = fitLine(t.L)
	return t
}

// celebrateFor is how long an agent that just finished (and was seen) cheers.
const celebrateFor = 6 * time.Second

// toolVerb is agents-deck's verb(): the lower-cased tool name, a few renamed.
func toolVerb(tool string) string {
	l := strings.ToLower(tool)
	switch l {
	case "compact":
		return "compacting"
	case "todowrite":
		return "plan"
	case "webfetch":
		return "web"
	case "websearch":
		return "search"
	case "multiedit", "notebookedit":
		return "edit"
	case "task":
		return "agent"
	}
	return l
}

// waitLine words a waiting tile. The tool name comes from the permission
// prompt's context ("bash \u00b7 ~/code/x"): only a bare lower-case word before
// the separator is used, never a path or free text, so the LAN variant
// cannot leak prompt content.
func waitLine(w *agents.Prompt) string {
	if w == nil {
		return "needs input"
	}
	switch w.Kind {
	case "question":
		return "pick 1 of " + strconv.Itoa(len(w.Options))
	case "permission":
		head, _, _ := strings.Cut(w.Context, " \u00b7 ")
		head = strings.TrimSpace(head)
		if head != "" && strings.Trim(head, "abcdefghijklmnopqrstuvwxyz0123456789_-") == "" {
			return "allow " + head + "?"
		}
		return "allow?"
	}
	return "needs input"
}

// agoOf is agents-deck's ago(): "12s", "4m", "3h", "2d".
func agoOf(since, now time.Time) string {
	s := int64(0)
	if !since.IsZero() {
		s = max(0, int64(now.Sub(since)/time.Second))
	}
	switch {
	case s < 60:
		return itoa(s) + "s"
	case s < 3600:
		return itoa(s/60) + "m"
	case s < 86400:
		return itoa(s/3600) + "h"
	}
	return itoa(s/86400) + "d"
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

// contextPct is round(100*used/window), clamped to 0..100; 0 without a window.
func contextPct(used, window int64) int {
	if window <= 0 {
		return 0
	}
	return int(min(100, max(0, math.Round(100*float64(used)/float64(window)))))
}

func promptOf(a agents.Agent) *AgentPrompt {
	return promptWith(a, PromptTitleChars, PromptDetailChars, PromptOptions)
}

// promptWith is a's prompt with the title, the detail and the options cut to
// the given maxima; the other fields keep the cube's limits.
func promptWith(a agents.Agent, titleN, detailN, optsN int) *AgentPrompt {
	w := a.Waiting
	q := &AgentPrompt{I: fitN(a.ID, TileIDChars), P: fitN(w.ID, PromptIDChars), K: fitN(w.Kind, PromptKindChars),
		T: fitMarked(w.Title, titleN), D: fitMarked(w.Detail, detailN), C: fitN(w.Context, PromptCtxChars)}
	for _, o := range w.Options[:min(len(w.Options), optsN)] {
		q.O = append(q.O, PromptOption{K: fitN(o.Key, OptionKeyChars), L: fitN(o.Label, OptionLabelChars)})
	}
	if a.Pane == "" {
		q.X = "tmux"
	}
	return q
}

// fitBudget trims p until it marshals to at most budget bytes, one step at a
// time, least valuable first: the extra cards from the end, the prompt's
// detail (q.cut = 1), options beyond 4, tiles down to 4 (the last one not waiting each time), the
// prompt (qc = 1), and as a last resort (unreachable with today's field
// limits) the Agents card. agentsAt is the Agents card's index (-1: none);
// cards from extraFrom on are the extra cards.
func fitBudget(p *Payload, agentsAt, extraFrom, budget int) {
	ac := func() *Card {
		if agentsAt < 0 || agentsAt >= len(p.Cards) {
			return nil
		}
		return &p.Cards[agentsAt]
	}
	steps := []func() bool{
		func() bool {
			if len(p.Cards) > extraFrom {
				p.Cards = p.Cards[:len(p.Cards)-1]
				return true
			}
			return false
		},
		func() bool {
			if c := ac(); c != nil && c.Q != nil && c.Q.D != "" {
				c.Q.D, c.Q.Cut = "", 1
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
				// The last tile that is not waiting, so a waiting one stays.
				drop := len(c.A) - 1
				for i := drop; i >= 0; i-- {
					if c.A[i].K != "wait" {
						drop = i
						break
					}
				}
				c.A = slices.Delete(c.A, drop, drop+1)
				return true
			}
			return false
		},
		func() bool {
			if c := ac(); c != nil && c.Q != nil {
				c.Q, c.QCut = nil, 1 // only the Full variant has a q to drop
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
