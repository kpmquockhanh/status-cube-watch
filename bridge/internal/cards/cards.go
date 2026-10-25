// Package cards turns usage data into the card deck the cube renders: a port
// of the Node card builder, golden-tested against it (testdata/gen.mjs).
//
// All formatting happens here, on the host: the firmware just draws whatever
// strings it is handed. The usage card (both rate-limit windows as two rings,
// plus the unread-mail badge) is always there; the spend and token cards are
// added only with ExtraCards. Every string leaves as printable ASCII cut to
// the firmware's buffer sizes (Fit).
package cards

import (
	"math"
	"regexp"
	"sort"
	"time"

	"claude-status-cube/bridge/internal/agents"
	"claude-status-cube/bridge/internal/limits"
	"claude-status-cube/bridge/internal/mail"
	"claude-status-cube/bridge/internal/sources"
)

// Row is one legend line under the dual ring.
type Row struct {
	K string `json:"k"`
	P string `json:"p"`
	R string `json:"r"`
}

// Card is one card of the payload contract (see the repo CLAUDE.md).
type Card struct {
	T    string       `json:"t"`
	V    string       `json:"v"`
	S1   string       `json:"s1"`
	S2   string       `json:"s2"`
	C    string       `json:"c"`
	G    *int         `json:"g,omitempty"`
	C2   string       `json:"c2,omitempty"`
	G2   *int         `json:"g2,omitempty"`
	Rows []Row        `json:"rows,omitempty"`
	M    string       `json:"m,omitempty"`
	MC   string       `json:"mc,omitempty"`
	A    []Tile       `json:"a,omitempty"`  // Agents card: the agents
	Q    *AgentPrompt `json:"q,omitempty"`  // Agents card: the longest-waiting prompt (Full only)
	QCut int          `json:"qc,omitempty"` // Agents card: 1 = the payload budget dropped q (Full only)
	D    *AgentDetail `json:"d,omitempty"`  // Agents card: one agent's detail (Full only, ?detail=<id>)
}

// Payload is what GET /api/status serves.
type Payload struct {
	V     int    `json:"v"`
	Ts    int64  `json:"ts"`
	Src   string `json:"src"`
	Cards []Card `json:"cards"`
}

// Opts are buildPayload's options. Error "" = the source did not fail.
type Opts struct {
	Source           string
	HasRequestCounts bool
	Billed           *sources.Billed
	Error            string
	Limits           *limits.Limits
	Mail             *mail.Mail
	ExtraCards       bool
	Agents           []agents.Agent // the open agents
	ShowAgents       bool           // add the Agents card (when at least one agent is open)
	Full             bool           // loopback variant: the Agents card carries the prompt
	// Hats is the per-session hat override (keyed by HatKey): code 0..103, -1 = no hat; ok=false
	// (or nil) = agents-deck's automatic hat.
	Hats func(key string) (code int, ok bool)
}

// Build builds the deck. now carries the local zone: today/month boundaries
// use now.Location().
//
// The Agents card follows the usage card, and the whole payload is trimmed to
// MaxPayloadBytes (fitBudget).
func Build(events []sources.Event, o Opts, now time.Time) Payload {
	return build(events, o, now, MaxPayloadBytes)
}

func build(events []sources.Event, o Opts, now time.Time, budget int) Payload {
	cards := []Card{usageCard(o.Limits, o.Mail, now.UnixMilli())}
	agentsAt := -1
	if o.ShowAgents {
		if c := agentsCardOf(o.Agents, o.Full, now, o.Hats); c != nil {
			agentsAt = len(cards)
			cards = append(cards, *c)
		}
	}
	extraFrom := len(cards)
	if o.ExtraCards {
		cards = append(cards, spendCards(events, o, now)...)
	}
	if o.Error != "" {
		// 28 characters is what fits across the panel in the text card's s1 font.
		cards = append([]Card{{T: "BRIDGE ERROR", V: "offline", S1: Fit(o.Error, 29), C: "red"}}, cards...)
		extraFrom++
		if agentsAt >= 0 {
			agentsAt++
		}
	}
	for i := range cards {
		cards[i] = fitCard(cards[i])
	}
	p := Payload{V: 1, Ts: now.Unix(), Src: o.Source, Cards: cards}
	fitBudget(&p, agentsAt, extraFrom, budget)
	return p
}

// tone sets the 60/85 thresholds shared with the firmware, preview.html and
// the Mac helper.
func tone(pct int) string {
	switch {
	case pct >= 85:
		return "accent"
	case pct >= 60:
		return "amber"
	}
	return "ink"
}

type windowState struct {
	pct    *int // nil: lapsed, the old percentage is dropped
	known  bool
	lapsed bool
	left   string
}

// windowView reads one window; nil when there is no reading. A reset already
// in the past means the window lapsed: its percentage belongs to the old
// window, so the ring goes to an empty track and the caption says so.
func windowView(w *limits.Window, nowMs int64) *windowState {
	if w == nil || w.Utilization == nil {
		return nil
	}
	resetsAt, known := parseJSDate(w.ResetsAt)
	v := &windowState{known: known, lapsed: known && resetsAt <= nowMs, left: "--"}
	if !v.lapsed {
		p := int(jsRound(*w.Utilization))
		v.pct = &p
	}
	if known {
		v.left = "idle"
		if !v.lapsed {
			v.left = until(resetsAt - nowMs)
		}
	}
	return v
}

// parseJSDate stands in for Date.parse on the ISO 8601 timestamps the usage
// endpoint sends, at Date's millisecond precision.
func parseJSDate(s string) (int64, bool) {
	t, err := time.Parse(time.RFC3339Nano, s)
	if err != nil {
		return 0, false
	}
	return t.UnixMilli(), true
}

var (
	reNoLogin  = regexp.MustCompile(`(?i)no Claude Code login`)
	reStarting = regexp.MustCompile(`(?i)starting`)
	reTimeout  = regexp.MustCompile(`(?i)timeout`)
	reStatus   = regexp.MustCompile(`\b([1-5]\d\d)\b`)
	reBadJSON  = regexp.MustCompile(`(?i)bad JSON`)
)

// shortReason boils a failed limits read down to the caption's word or two;
// the full message rides along in s2.
func shortReason(e string) string {
	switch {
	case reNoLogin.MatchString(e):
		return "no login"
	case reStarting.MatchString(e):
		return "starting"
	case reTimeout.MatchString(e):
		return "timeout"
	}
	if m := reStatus.FindStringSubmatch(e); m != nil {
		return "HTTP " + m[1]
	}
	if reBadJSON.MatchString(e) {
		return "bad reply"
	}
	return "offline"
}

// mailBadge sets the unread count and its accent; "--" in red when there has
// never been a reading. Left out when mail is not set up.
func mailBadge(c *Card, m *mail.Mail) {
	if m == nil {
		return
	}
	if m.Unread == nil {
		c.M, c.MC = "--", "red"
		return
	}
	c.M = thousands(*m.Unread)
	switch {
	case m.Stale:
		c.MC = "red"
	case *m.Unread == 0:
		c.MC = "green"
	default:
		c.MC = "amber"
	}
}

func intp(n int) *int { return &n }

// usageCard: both rate-limit windows as ONE card of two concentric rings,
// the 5h session outside (g, c), the 7d week inside (g2, c2).
func usageCard(l *limits.Limits, m *mail.Mail, nowMs int64) Card {
	var five, seven *windowState
	errMsg := ""
	if l != nil {
		five = windowView(l.FiveHour, nowMs)
		seven = windowView(l.SevenDay, nowMs)
		errMsg = l.Error
	}
	has := func(w *windowState) bool { return w != nil && w.pct != nil }
	row := func(k string, w *windowState) Row {
		r := Row{K: k, P: "--"}
		if has(w) {
			r.P = itoa(int64(*w.pct)) + "%"
		}
		if w != nil && w.known {
			r.R = w.left
			if w.lapsed {
				r.R = "reset"
			}
		}
		return r
	}
	c := Card{T: "CLAUDE", V: "--", C: "ink", G: intp(-1), C2: "ink", G2: intp(-1),
		Rows: []Row{row("5H", five), row("7D", seven)}}
	if five != nil {
		c.V = five.left
	}
	// While a read is failing the caption says why, even over a last good
	// reading that is still on screen.
	switch {
	case errMsg != "":
		c.S1 = shortReason(errMsg)
	case five == nil:
		c.S1 = "no data"
	case !five.known:
		c.S1 = ""
	case five.lapsed:
		c.S1 = "reset"
	default:
		c.S1 = "to reset"
	}
	switch {
	case errMsg != "":
		c.S2 = errMsg
	case five == nil && seven == nil:
		c.S2 = "unavailable"
	}
	if has(five) {
		c.C, c.G = tone(*five.pct), intp(*five.pct)
	}
	if has(seven) {
		c.C2, c.G2 = tone(*seven.pct), intp(*seven.pct)
	}
	mailBadge(&c, m)
	return c
}

// Firmware buffer sizes (payload.h), NUL included, so a field gets one less.
func fitCard(c Card) Card {
	c.T, c.V, c.S1, c.S2, c.M = Fit(c.T, 24), Fit(c.V, 24), Fit(c.S1, 40), Fit(c.S2, 40), Fit(c.M, 8)
	if c.Rows != nil {
		rows := make([]Row, len(c.Rows))
		for i, r := range c.Rows {
			rows[i] = Row{K: Fit(r.K, 6), P: Fit(r.P, 8), R: Fit(r.R, 12)}
		}
		c.Rows = rows
	}
	return c
}

type totalsT struct {
	cost                                 float64
	input, cacheRead, cacheWrite, output int64
	count, allTokens, inTokens           int64
}

func totals(events []sources.Event) totalsT {
	var t totalsT
	for _, e := range events {
		t.cost += e.Cost
		t.input += e.Input
		t.cacheRead += e.CacheRead
		t.cacheWrite += e.CacheWrite
		t.output += e.Output
		t.count++
	}
	t.allTokens = t.input + t.cacheRead + t.cacheWrite + t.output
	t.inTokens = t.input + t.cacheRead + t.cacheWrite
	return t
}

type ranked struct {
	key string
	sum float64
}

// topBy sums cost per key ("" reads as "unknown"), highest first, ties in
// first-seen order (JS sort is stable over Map insertion order).
func topBy(events []sources.Event, key func(sources.Event) string) []ranked {
	var out []ranked
	index := map[string]int{}
	for _, e := range events {
		k := key(e)
		if k == "" {
			k = "unknown"
		}
		i, ok := index[k]
		if !ok {
			i = len(out)
			index[k] = i
			out = append(out, ranked{key: k})
		}
		out[i].sum += e.Cost
	}
	sort.SliceStable(out, func(a, b int) bool { return out[a].sum > out[b].sum })
	return out
}

func filterSince(events []sources.Event, ms int64) []sources.Event {
	var out []sources.Event
	for _, e := range events {
		if e.T >= ms {
			out = append(out, e)
		}
	}
	return out
}

func plural(n int64, word string) string {
	if n == 1 {
		return itoa(n) + " " + word
	}
	return itoa(n) + " " + word + "s"
}

// spendCards is the original spend deck, behind ExtraCards.
func spendCards(events []sources.Event, o Opts, now time.Time) []Card {
	nowMs := now.UnixMilli()
	loc := now.Location()
	y, mo, d := now.Date()
	startOfToday := time.Date(y, mo, d, 0, 0, 0, 0, loc)
	startOfMonth := time.Date(y, mo, 1, 0, 0, 0, 0, loc)

	today := filterSince(events, startOfToday.UnixMilli())
	month := filterSince(events, startOfMonth.UnixMilli())
	dayT := totals(today)
	monthT := totals(month)

	// The cost report is billing truth; prefer it over our estimate. Its days
	// are keyed by the local calendar date.
	monthCost, todayCost := monthT.cost, dayT.cost
	if o.Billed != nil {
		monthCost = o.Billed.Total
		if v, ok := o.Billed.ByDay[now.Format("2006-01-02")]; ok {
			todayCost = v
		}
	}

	// Straight-line projection from the month so far.
	daysIn := max(1, float64(nowMs-startOfMonth.UnixMilli())/86_400_000)
	daysInMonth := time.Date(y, mo+1, 0, 0, 0, 0, 0, loc).Day()
	projected := monthCost / daysIn * float64(daysInMonth)

	cacheHit := int64(0)
	if cacheTotal := dayT.cacheRead + dayT.cacheWrite; cacheTotal > 0 {
		cacheHit = jsRound(float64(dayT.cacheRead) / float64(cacheTotal) * 100)
	}

	modelRank := topBy(today, func(e sources.Event) string { return e.Model })
	projectRank := topBy(today, func(e sources.Event) string { return e.Project })

	reqLine := plural(dayT.count, "request")
	if !o.HasRequestCounts {
		reqLine = plural(int64(len(modelRank)), "model")
	}

	tokenTone := "amber"
	if cacheHit >= 80 {
		tokenTone = "green"
	}

	topModel := Card{T: "TOP MODEL", V: "--", S1: "no usage today", S2: "only model today", C: "violet"}
	if len(modelRank) > 0 {
		spend := dayT.cost
		if spend == 0 {
			spend = 1
		}
		topModel.V = prettyModel(modelRank[0].key)
		topModel.S1 = money(modelRank[0].sum) + " - " + itoa(jsRound(modelRank[0].sum/spend*100)) + "% of spend"
	}
	if len(modelRank) > 1 {
		topModel.S2 = "then " + prettyModel(modelRank[1].key)
	}

	topProject := Card{T: "TOP PROJECT", V: "--", S1: "idle", S2: "no activity", C: "green"}
	if o.Source == "admin" {
		topProject.T = "TOP WORKSPACE"
	}
	if len(projectRank) > 0 {
		topProject.V = projectRank[0].key
		topProject.S1 = money(projectRank[0].sum)
	}
	if len(events) > 0 {
		topProject.S2 = "active " + ago(nowMs-events[len(events)-1].T)
	}

	return []Card{
		{T: "TODAY", V: money(todayCost), S1: tokens(dayT.allTokens) + " tokens", S2: reqLine, C: "accent"},
		{
			T:  "THIS MONTH",
			V:  money(monthCost),
			S1: "proj. " + money(projected),
			S2: "day " + itoa(int64(math.Ceil(daysIn))) + " of " + itoa(int64(daysInMonth)),
			C:  "blue",
		},
		{
			T:  "TOKENS TODAY",
			V:  tokens(dayT.allTokens),
			S1: "in " + tokens(dayT.inTokens) + " / out " + tokens(dayT.output),
			S2: "cache hit " + itoa(cacheHit) + "%",
			C:  tokenTone,
		},
		topModel,
		topProject,
	}
}
