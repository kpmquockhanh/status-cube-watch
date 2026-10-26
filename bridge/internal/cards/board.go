package cards

import (
	"time"

	"claude-status-cube/bridge/internal/agents"
)

// The web board's prompt maxima: a browser has room for the whole question,
// so only a runaway title or detail is cut. Options stop at 9, the one-digit
// keys an answer can type.
const (
	BoardTitleChars  = 200
	BoardDetailChars = 4000
	BoardOptions     = 9
)

// Board is GET /api/board: every open agent for the bridge's web board
// (loopback only, never on the LAN), each with the cube's tile, its detail
// screen and, while it waits, its prompt. No payload budget applies.
type Board struct {
	Ts     int64        `json:"ts"`
	S1     string       `json:"s1"` // the Agents card's caption: "1 NEEDS YOU", "2 RUNNING", "ALL IDLE", "NO AGENTS"
	C      string       `json:"c"`  // its accent
	Agents []BoardAgent `json:"agents"`
}

// BoardAgent is a Tile (its fields inline) plus the detail and the prompt.
type BoardAgent struct {
	Tile
	D *AgentDetail `json:"d"`
	Q *AgentPrompt `json:"q,omitempty"`
}

// BuildBoard lists the agents in the Agents card's order. hats is the
// per-session override (Opts.Hats); nil = every agent wears its automatic hat.
func BuildBoard(list []agents.Agent, now time.Time, hats func(string) (int, bool)) Board {
	b := Board{Ts: now.Unix(), S1: "NO AGENTS", C: "ink", Agents: []BoardAgent{}}
	if c := agentsCardOf(list, false, now, hats); c != nil {
		b.S1, b.C = c.S1, c.C
	}
	for _, a := range sortAgents(list) {
		auto := true
		if hats != nil {
			_, overridden := hats(HatKey(a))
			auto = !overridden
		}
		ba := BoardAgent{Tile: tileOf(a, now, hats), D: detailOf(a, auto, now)}
		if a.Status == agents.Waiting && a.Waiting != nil {
			ba.Q = promptWith(a, BoardTitleChars, BoardDetailChars, BoardOptions)
		}
		b.Agents = append(b.Agents, ba)
	}
	return b
}
