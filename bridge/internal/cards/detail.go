package cards

import (
	"cmp"
	"math"
	"strconv"
	"strings"
	"time"
	"unicode/utf8"

	"claude-status-cube/bridge/internal/agents"
)

// Agent detail field maxima, in BYTES of the UTF-8 string (a firmware buffer
// is max + 1). s, w, c and m hold the two-byte sep. A cut field ends in "..."
// within the limit.
const (
	DetailTitleChars  = 40  // AgentDetail.T
	DetailStatusChars = 24  // AgentDetail.S
	DetailDoingChars  = 48  // AgentDetail.W
	DetailCtxChars    = 24  // AgentDetail.C
	DetailPromptChars = 160 // AgentDetail.P
	DetailMetaChars   = 48  // AgentDetail.M
	DetailErrChars    = 60  // AgentDetail.E
	DetailSubsChars   = 16  // AgentDetail.N
)

// detailPromptShort is what the budget cuts AgentDetail.P to before dropping it.
const detailPromptShort = 60

// AgentDetail is one agent's detail screen, pre-formatted. It rides on the
// Agents card only in the loopback payload and only for ?detail=<id>.
type AgentDetail struct {
	I  string `json:"i"`           // agent id
	T  string `json:"t"`           // title (else folder)
	S  string `json:"s"`           // "running · 38m"
	W  string `json:"w"`           // doing now: "edit · ~/x/src/a.ts", or the status word
	C  string `json:"c"`           // "41% · 410k / 1m"; "" without a window
	P  string `json:"p,omitempty"` // last prompt
	M  string `json:"m,omitempty"` // "opus 5.5 · ~/x · main"
	E  string `json:"e,omitempty"` // error text
	N  string `json:"n,omitempty"` // "3 subagents"
	HA int    `json:"ha"`          // 1 = no hat override applies (automatic)
}

// cutBytes keeps s within n bytes: a longer one is cut on a rune boundary and
// ends in "...".
func cutBytes(s string, n int) string {
	if len(s) <= n {
		return s
	}
	end := max(0, n-3)
	for end > 0 && !utf8.RuneStart(s[end]) {
		end--
	}
	return strings.TrimRight(s[:end], " ") + "..."
}

// fitBytes folds s to ASCII with Fit, then cuts it to n bytes.
func fitBytes(s string, n int) string { return cutBytes(Fit(s, math.MaxInt), n) }

// joinParts joins the non-empty parts with sep.
func joinParts(parts ...string) string {
	var out []string
	for _, p := range parts {
		if p != "" {
			out = append(out, p)
		}
	}
	return strings.Join(out, sep)
}

// detailCount words a token count: 410, 41k, 1.2m, 1m.
func detailCount(n int64) string {
	scale := func(v float64, unit string) string {
		var s string
		if v < 10 {
			s = strconv.FormatFloat(math.Round(v*10)/10, 'f', 1, 64)
			s = strings.TrimSuffix(s, ".0")
		} else {
			s = strconv.FormatFloat(math.Round(v), 'f', 0, 64)
		}
		return s + unit
	}
	switch {
	case n < 1000:
		return strconv.FormatInt(max(0, n), 10)
	case n < 999_500:
		return scale(float64(n)/1000, "k")
	}
	return scale(float64(n)/1e6, "m")
}

func statusWord(s agents.Status) string {
	switch s {
	case agents.Waiting:
		return "needs you"
	case agents.Running:
		return "running"
	case agents.Starting:
		return "starting"
	case agents.Error:
		return "error"
	case agents.Exited:
		return "exited"
	}
	return "idle"
}

func detailOf(a agents.Agent, hatAuto bool, now time.Time) *AgentDetail {
	word := statusWord(a.Status)
	d := &AgentDetail{I: fitN(a.ID, TileIDChars), T: fitBytes(cmp.Or(a.Title, a.Folder), DetailTitleChars)}
	d.S = cutBytes(word+sep+agoOf(a.StatusSince, now), DetailStatusChars)
	d.W = word
	if a.Status == agents.Running {
		d.W = "thinking"
		if a.Activity != nil && a.Activity.Tool != "" {
			verb := Fit(toolVerb(a.Activity.Tool), math.MaxInt)
			d.W = verb
			if det := a.Activity.Detail; det != "" {
				if strings.HasPrefix(det, "/") || strings.HasPrefix(det, "~") {
					det = agents.ShortPath(det)
				}
				d.W = verb + sep + Fit(det, math.MaxInt)
			}
		}
		d.W = cutBytes(d.W, DetailDoingChars)
	}
	if a.Context.Window > 0 {
		d.C = cutBytes(strconv.Itoa(contextPct(a.Context.Used, a.Context.Window))+"%"+sep+
			detailCount(a.Context.Used)+" / "+detailCount(a.Context.Window), DetailCtxChars)
	}
	d.P = fitBytes(a.LastPrompt, DetailPromptChars)
	d.M = cutBytes(joinParts(Fit(cmp.Or(a.ModelLabel, a.Model), math.MaxInt),
		Fit(agents.ShortPath(cmp.Or(a.Cwd, a.Folder)), math.MaxInt), Fit(a.Branch, math.MaxInt)), DetailMetaChars)
	if a.Status == agents.Error {
		d.E = fitBytes(a.ErrorText, DetailErrChars)
	}
	switch n := len(a.Subagents); {
	case n == 1:
		d.N = "1 subagent"
	case n > 1:
		d.N = strconv.Itoa(n) + " subagents"
	}
	if hatAuto {
		d.HA = 1
	}
	return d
}

// WithDetail returns p with a's detail on the Agents card, then trims to
// MaxPayloadBytes: d.P is cut to 60 bytes, then dropped, then d is dropped.
// p is not modified (the server shares it). A payload with no Agents card is
// returned as is. Loopback only: the caller decides who may ask.
func WithDetail(p Payload, a agents.Agent, hatAuto bool, now time.Time) Payload {
	at := -1
	for i := range p.Cards {
		if p.Cards[i].T == "AGENTS" {
			at = i
			break
		}
	}
	if at < 0 {
		return p
	}
	p.Cards = append([]Card(nil), p.Cards...)
	p.Cards[at].D = detailOf(a, hatAuto, now)
	d := p.Cards[at].D
	if !fits(p, MaxPayloadBytes) && len(d.P) > detailPromptShort {
		d.P = cutBytes(d.P, detailPromptShort)
	}
	if !fits(p, MaxPayloadBytes) {
		d.P = ""
	}
	if !fits(p, MaxPayloadBytes) {
		p.Cards[at].D = nil
	}
	return p
}
