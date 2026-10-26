// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/daemon/daemon.go (Hook, Answer, subagents, scanMenu) and backend/internal/daemon/live.go (syncLive, liveStatus).

package agents

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"sort"
	"strings"
	"sync"
	"time"
)

// Errors of Answer.
var (
	ErrNotFound  = errors.New("no such agent")
	ErrStale     = errors.New("that prompt is no longer open")
	ErrBadKey    = errors.New("not one of the prompt's options")
	ErrNotInTmux = errors.New("the agent does not run in a tmux pane")
)

const (
	exitedTTL     = 60 * time.Second // an exited agent stays this long
	missGrace     = 15 * time.Second // a hook this recent proves the session alive
	maxMisses     = 2                // successful discoveries in a row without it
	turnWindow    = 10 * time.Minute // hookless: a quiet transcript ends the turn
	subWindow     = 2 * time.Minute
	subToolWindow = 30 * time.Minute
	paneHistory   = 40 // lines of history read for prompt options
)

// The dialog renders a moment after the hook fires: read the pane twice.
var rescanWaits = []time.Duration{250 * time.Millisecond, 900 * time.Millisecond}

var (
	paneRe = regexp.MustCompile(`^%[0-9]+$`)
	keyRe  = regexp.MustCompile(`^[1-9]$`) // one digit: MaxOptions
)

// TrackerDeps are the tracker's seams; zero values get the real thing.
type TrackerDeps struct {
	Tmux     *Tmux
	Discover Discoverer
	Projects string // ~/.claude/projects (or $CLAUDE_CONFIG_DIR/projects)
	Now      func() time.Time
	After    func(time.Duration, func()) // time.AfterFunc seam for the option re-scan
}

// Tracker holds every known Claude Code session. One mutex guards the
// entries; it is never held while a command runs or a transcript is read.
type Tracker struct {
	d TrackerDeps

	// Every t.mu Lock is paired with a deferred Unlock in the same small
	// function, so a panic can never leave the tracker locked.
	mu           sync.Mutex
	entries      map[string]*entry
	prompts      int    // prompt ids handed out, for the tracker's lifetime
	promptPrefix string // prefixes every prompt id: unique per tracker start
	lastRun      uint64 // the discovery run whose misses were counted last
	counted      bool   // lastRun is set

	subMu   sync.Mutex
	subs    map[int]chan struct{}
	nextSub int
}

// DefaultProjects is where Claude Code keeps session transcripts.
func DefaultProjects() string {
	if d := os.Getenv("CLAUDE_CONFIG_DIR"); d != "" {
		return filepath.Join(d, "projects")
	}
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".claude", "projects")
}

func NewTracker(d TrackerDeps) *Tracker {
	if d.Projects == "" {
		d.Projects = DefaultProjects()
	}
	if d.Now == nil {
		d.Now = time.Now
	}
	if d.After == nil {
		d.After = func(w time.Duration, f func()) { time.AfterFunc(w, f) }
	}
	if d.Tmux == nil {
		d.Tmux = NewTmux()
	}
	if d.Discover == nil {
		d.Discover = NewDiscovery(d.Projects)
	}
	return &Tracker{d: d, entries: map[string]*entry{}, subs: map[int]chan struct{}{},
		promptPrefix: promptIDPrefix(d.Now())}
}

// newEntry adds an agent for sessionID; its prompt ids come from the
// tracker's counter. Called with t.mu held.
func (t *Tracker) newEntry(id, sessionID string, status Status, now time.Time) *entry {
	e := &entry{promptSeq: &t.prompts, promptPfx: t.promptPrefix}
	e.ID, e.SessionID = id, sessionID
	e.SetStatus(status, now)
	t.entries[id] = e
	return e
}

// Hook folds one Claude Code hook payload into its agent, creating the agent
// for a session it has not seen yet.
func (t *Tracker) Hook(body []byte) error {
	var ev Event
	if err := json.Unmarshal(body, &ev); err != nil {
		return err
	}
	if ev.SessionID == "" {
		return errors.New("hook without a session_id")
	}
	subagent := strings.Contains(ev.TranscriptPath, "/subagents/")
	ev.TranscriptPath = t.trustedTranscript(ev.TranscriptPath)
	id := agentID(ev.SessionID)
	changed, scan, pane, promptID := t.applyHookEvent(id, ev, subagent, t.d.Now())
	if changed {
		t.notify()
	}
	if scan && pane != "" && promptID != "" {
		t.rescan(id, promptID, 0)
	}
	return nil
}

// applyEvent is apply; tests swap it to check the lock survives a panic.
var applyEvent = apply

// applyHookEvent folds ev into agent id under the lock; pane and promptID
// are what a re-scan needs.
func (t *Tracker) applyHookEvent(id string, ev Event, subagent bool, now time.Time) (changed, scan bool, pane, promptID string) {
	t.mu.Lock()
	defer t.mu.Unlock()
	e := t.entries[id]
	created := false
	if e == nil {
		// A subagent's hooks belong to its parent; an end is no news.
		if subagent || ev.Name == "SessionEnd" {
			return false, false, "", ""
		}
		e = t.newEntry(id, ev.SessionID, Starting, now)
		created = true
	}
	e.HookSeen, e.LastHook, e.Missed = true, now, 0
	changed, scan = applyEvent(e, ev, now)
	if e.Status == Starting { // any hook proves Claude is up
		e.SetStatus(Idle, now)
		changed = true
	}
	changed = retitle(e) || changed || created
	if e.Waiting != nil {
		promptID = e.Waiting.ID
	}
	return changed, scan, e.Pane, promptID
}

// rescan reads the pane after each of rescanWaits, replacing the guessed
// options of prompt promptID with the ones on screen; it stops once that
// prompt is no longer the open one.
func (t *Tracker) rescan(id, promptID string, step int) {
	if step >= len(rescanWaits) {
		return
	}
	t.d.After(rescanWaits[step], func() {
		if t.refineFromPane(id, promptID) {
			t.rescan(id, promptID, step+1)
		}
	})
}

// refineFromPane refines prompt promptID from the pane; false once that
// prompt is gone.
func (t *Tracker) refineFromPane(id, promptID string) bool {
	pane := t.openPromptPane(id, promptID)
	if pane == "" {
		return false
	}
	screen := t.d.Tmux.Capture(context.Background(), pane, paneHistory)
	changed, still := t.refineOpenPrompt(id, promptID, pane, screen)
	if changed {
		t.notify()
	}
	return still
}

// openPrompt: prompt promptID is agent e's open one.
func openPrompt(e *entry, promptID string) bool {
	return e != nil && e.Waiting != nil && e.Waiting.ID == promptID
}

// openPromptPane is the pane of agent id while promptID is its open prompt;
// "" otherwise (or when not in tmux).
func (t *Tracker) openPromptPane(id, promptID string) string {
	t.mu.Lock()
	defer t.mu.Unlock()
	if e := t.entries[id]; openPrompt(e, promptID) {
		return e.Pane
	}
	return ""
}

// refineOpenPrompt refines prompt promptID from screen, read from pane, if
// that prompt is still open in that pane (still).
func (t *Tracker) refineOpenPrompt(id, promptID, pane, screen string) (changed, still bool) {
	t.mu.Lock()
	defer t.mu.Unlock()
	e := t.entries[id]
	if !openPrompt(e, promptID) || e.Pane != pane {
		return false, false
	}
	return RefineOptions(e.Waiting, screen), true
}

// Sync runs discovery, maps sessions to tmux panes, tails transcripts,
// tracks subagents, reads the status of sessions that send no hooks, scans
// starting agents for arrow-key menus and drops agents that are gone.
func (t *Tracker) Sync(ctx context.Context) {
	lives, run, ok := t.d.Discover.Sessions(ctx)
	panes := t.d.Tmux.Panes(ctx)
	now := t.d.Now()

	changed, jobs := t.syncStart(lives, run, ok, panes, now)
	for i := range jobs {
		jobs[i].run(ctx, t.d.Tmux, now)
	}
	if t.syncFinish(jobs, now) || changed {
		t.notify()
	}
}

// syncStart folds discovery and the tmux panes in and makes the read jobs.
func (t *Tracker) syncStart(lives []Live, run uint64, ok bool, panes map[string]Pane, now time.Time) (bool, []syncJob) {
	t.mu.Lock()
	defer t.mu.Unlock()
	var changed bool
	if ok {
		var seen map[string]bool
		changed, seen = t.applyDiscovery(lives, now)
		// A cached list is the same run again: its misses count once.
		if !t.counted || run != t.lastRun {
			t.countMisses(seen, now)
			t.lastRun, t.counted = run, true
		}
	}
	changed = t.mapPanes(panes) || changed
	return changed, t.syncJobs()
}

// syncFinish folds the jobs' reads in and drops agents that are gone.
func (t *Tracker) syncFinish(jobs []syncJob, now time.Time) bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	changed := false
	for i := range jobs {
		changed = t.applyJob(&jobs[i], now) || changed
	}
	return t.expire(now) || changed
}

// applyDiscovery adds newly found sessions and refreshes what discovery
// knows about the others (a successful discovery only); seen holds the
// agent ids it listed.
func (t *Tracker) applyDiscovery(lives []Live, now time.Time) (changed bool, seen map[string]bool) {
	seen = map[string]bool{}
	for _, l := range lives {
		if !IsSessionID(l.SessionID) {
			continue
		}
		id := agentID(l.SessionID)
		seen[id] = true
		transcript := t.trustedTranscript(l.Transcript)
		e := t.entries[id]
		if e == nil {
			// No transcript yet: a fresh session, maybe still in a dialog
			// (workspace trust) that no hook reports.
			status := Idle
			if transcript == "" {
				status = Starting
			}
			e = t.newEntry(id, l.SessionID, status, now)
			changed = true
		}
		live := l
		e.Live, e.TTY, e.Missed = &live, l.TTY, 0
		if e.Cwd == "" && l.Cwd != "" {
			e.Cwd, e.Folder = l.Cwd, filepath.Base(l.Cwd)
			changed = true
		}
		if e.Transcript == "" && transcript != "" {
			e.Transcript, e.Offset, e.Pending, e.SkipLine = transcript, 0, "", false
		}
		changed = retitle(e) || changed
	}
	return changed, seen
}

// countMisses counts one discovery run that did not list an agent.
func (t *Tracker) countMisses(seen map[string]bool, now time.Time) {
	for id, e := range t.entries {
		if seen[id] || e.Status == Exited {
			continue
		}
		if now.Sub(e.LastHook) < missGrace {
			e.Missed = 0 // a (cached) discovery may predate it
			continue
		}
		e.Missed++
	}
}

// mapPanes matches each session's terminal to a tmux pane.
func (t *Tracker) mapPanes(panes map[string]Pane) bool {
	changed := false
	for _, e := range t.entries {
		var p Pane
		if e.TTY != "" && paneRe.MatchString(panes[e.TTY].ID) {
			p = panes[e.TTY]
		}
		if p.ID != e.Pane || p.Order != e.PaneOrder {
			e.Pane, e.PaneOrder = p.ID, p.Order
			changed = true
		}
	}
	return changed
}

// syncJob is one agent's file and pane reads, done without the lock.
type syncJob struct {
	id          string
	snap        entry // the entry when the job was made
	tailed      entry // snap after tail
	tailChanged bool
	subs        []Subagent
	running     bool // hookless: transcript says a turn is going on
	done        bool // hookless: the transcript ends in Claude's final reply
	tool        string
	menu        bool // screen was captured for a menu scan
	screen      string
}

func (t *Tracker) syncJobs() []syncJob {
	jobs := make([]syncJob, 0, len(t.entries))
	for id, e := range t.entries {
		jobs = append(jobs, syncJob{id: id, snap: *e})
	}
	sort.Slice(jobs, func(i, j int) bool { return jobs[i].id < jobs[j].id })
	return jobs
}

func (j *syncJob) run(ctx context.Context, tm *Tmux, now time.Time) {
	j.tailed = j.snap
	if j.snap.Status == Exited {
		return
	}
	j.tailChanged = tail(&j.tailed, now)
	j.subs = subagents(j.snap.Transcript, now, subWindow, subToolWindow)
	if !j.snap.HookSeen && j.snap.Transcript != "" {
		j.running, j.tool, j.done = transcriptTurn(j.snap.Transcript, now, turnWindow)
	}
	if j.snap.Pane != "" && (j.snap.Status == Starting || j.snap.Menu) {
		j.menu, j.screen = true, tm.Capture(ctx, j.snap.Pane, 0)
	}
}

// applyJob folds a job's reads into its agent, unless the agent is gone.
func (t *Tracker) applyJob(j *syncJob, now time.Time) bool {
	e := t.entries[j.id]
	if e == nil {
		return false
	}
	changed := mergeTail(e, j)
	if j.snap.Status == Exited || e.Status == Exited {
		if e.Subagents != nil {
			e.Subagents = nil
			changed = true
		}
		return changed
	}
	if j.menu && e.Pane == j.snap.Pane && (e.Status == Starting || e.Menu) {
		changed = scanMenu(e, j.screen, now) || changed
	}
	if !e.HookSeen && !e.Menu {
		changed = liveStatus(e, j.running, j.done, j.tool, now) || changed
	}
	return applySubagents(e, j.subs, now) || changed
}

// mergeTail keeps what tail read, as long as nothing reset the transcript
// meanwhile. Its status changes (API error, end of a compaction) apply only
// when no hook moved the status since the job started.
func mergeTail(e *entry, j *syncJob) bool {
	s, r := &j.snap, &j.tailed
	if e.Transcript != s.Transcript || e.Offset != s.Offset || e.Pending != s.Pending || e.SkipLine != s.SkipLine {
		return false // re-read on the next Sync
	}
	e.Offset, e.Pending, e.SkipLine = r.Offset, r.Pending, r.SkipLine
	if !j.tailChanged {
		return false
	}
	e.Branch, e.AITitle, e.CustomTitle = r.Branch, r.AITitle, r.CustomTitle
	e.Model, e.ModelLabel, e.Context = r.Model, r.ModelLabel, r.Context
	if e.Status == s.Status && e.Activity == s.Activity && e.Waiting == s.Waiting &&
		e.ErrorText == s.ErrorText && e.CompactManual == s.CompactManual {
		e.Status, e.StatusSince, e.ErrorText = r.Status, r.StatusSince, r.ErrorText
		e.Activity, e.Waiting, e.CompactManual = r.Activity, r.Waiting, r.CompactManual
	}
	retitle(e)
	return true
}

// retitle sets the display title; true when it changed.
func retitle(e *entry) bool {
	if t := displayTitle(e); t != e.Title {
		e.Title = t
		return true
	}
	return false
}

// scanMenu turns an on-screen arrow-key menu into a prompt, and clears the
// prompt again once the menu is gone.
func scanMenu(e *entry, screen string, now time.Time) bool {
	title, opts, ok := ParseCursorMenu(screen)
	switch {
	case ok && !e.Menu:
		e.SetStatus(Waiting, now)
		e.Waiting = &Prompt{ID: e.NextPromptID(), Kind: "permission", Title: title,
			Context: ShortPath(e.Cwd), Options: opts}
		e.Menu = true
		return true
	case !ok && e.Menu:
		e.Menu = false
		if e.Status == Waiting {
			e.SetStatus(afterMenu(e), now)
		}
		return true
	}
	return false
}

// afterMenu is the status once a menu is gone: idle for an agent that sends
// hooks (Claude is up, and hooks own the status), else starting until its
// transcript says more.
func afterMenu(e *entry) Status {
	if e.HookSeen {
		return Idle
	}
	return Starting
}

// liveStatus reads the state of a session that sends no hooks off the end of
// its transcript: working until Claude's final reply of the turn, then idle
// (unseen once a turn finishes while we watch). done: the transcript ends in
// that final reply.
func liveStatus(e *entry, running, done bool, tool string, now time.Time) bool {
	changed := false
	if e.Status == Starting {
		if e.Transcript == "" {
			return false
		}
		e.SetStatus(Idle, now) // past any opening dialog
		changed = true
	}
	if l := e.Live; l != nil && l.Reported { // Claude Code's own report beats the transcript,
		// except after Claude's final reply: a session busy only on a background
		// task (a Monitor, a background shell) has finished its turn.
		running = l.Busy && !done
		if l.Blocked {
			if e.Status != Waiting {
				e.SetStatus(Waiting, now)
				e.Waiting = &Prompt{ID: e.NextPromptID(), Kind: "input",
					Title: "needs you", Context: "answer it in its own terminal"}
				return true
			}
			return changed
		}
		if e.Status == Waiting { // answered elsewhere
			e.SetStatus(Running, now)
			changed = true
		}
	}
	switch {
	case running && e.Status != Running:
		e.SetStatus(Running, now)
		e.Unseen = false
		changed = true
	case !running && e.Status == Running && !e.Background:
		e.SetStatus(Idle, now) // also clears the activity
		e.Unseen = true        // a turn finished while we watched
		changed = true
	}
	if running {
		cur := ""
		if e.Activity != nil {
			cur = e.Activity.Tool
		}
		if cur != tool {
			e.Activity = nil
			if tool != "" {
				e.Activity = &Activity{Tool: tool}
			}
			changed = true
		}
	}
	return changed
}

// applySubagents records the session's working subagents, and shows an idle
// agent as running while any are left, back to idle once they are done.
func applySubagents(e *entry, subs []Subagent, now time.Time) bool {
	if e.Status != Running {
		e.Background = false
	}
	changed := !slices.Equal(subs, e.Subagents)
	e.Subagents = subs
	switch {
	case len(subs) > 0 && e.Status == Idle:
		e.SetStatus(Running, now)
		e.Background = true
		changed = true
	case len(subs) == 0 && e.Background:
		e.SetStatus(Idle, now)
		e.Background = false
		changed = true
	}
	if e.Background {
		detail := "1 subagent working"
		if len(subs) != 1 {
			detail = fmt.Sprintf("%d subagents working", len(subs))
		}
		if e.Activity == nil || e.Activity.Detail != detail {
			e.Activity = &Activity{Tool: "Task", Detail: detail}
			changed = true
		}
	}
	return changed
}

// expire drops agents exited for exitedTTL and sessions discovery has missed
// maxMisses times in a row.
func (t *Tracker) expire(now time.Time) bool {
	changed := false
	for id, e := range t.entries {
		if e.Status == Exited && now.Sub(e.StatusSince) >= exitedTTL || e.Missed >= maxMisses {
			delete(t.entries, id)
			changed = true
		}
	}
	return changed
}

// answerClaim is what Answer needs once it has claimed a prompt.
type answerClaim struct {
	pane    string
	menu    bool
	idx     int      // the chosen option
	label   string   // its label
	options []string // every option's label (menu: to recognise it on screen)
}

// Answer types the chosen option of the open prompt into the agent's pane:
// the option's digit, or, for an unnumbered menu, arrow keys from wherever
// the cursor is now. A send that may have typed something (timeout, kill)
// keeps the prompt claimed, so a retry can never type twice; only a send
// tmux refused up front gives it back.
func (t *Tracker) Answer(ctx context.Context, agentID, promptID, key string) error {
	c, err := t.claimPrompt(agentID, promptID, key)
	if err != nil {
		return err
	}
	if c.menu {
		var keys []string
		keys, err = t.menuKeys(ctx, c)
		if err == nil {
			err = t.d.Tmux.SendKeys(ctx, c.pane, "", keys...)
		}
	} else {
		err = t.d.Tmux.SendKeys(ctx, c.pane, key)
	}
	if err != nil {
		if errors.Is(err, ErrStale) || sentNothing(err) {
			t.releasePrompt(agentID, promptID)
		}
		return err
	}
	if t.answered(agentID, promptID, c.label, t.d.Now()) {
		t.notify()
	}
	return nil
}

// claimPrompt checks the answer and marks the prompt as being answered.
func (t *Tracker) claimPrompt(agentID, promptID, key string) (answerClaim, error) {
	t.mu.Lock()
	defer t.mu.Unlock()
	e := t.entries[agentID]
	if e == nil {
		return answerClaim{}, ErrNotFound
	}
	p := e.Waiting
	// A prompt another Answer is typing (or may have typed) into is answered.
	if e.Status != Waiting || p == nil || p.ID != promptID || e.Answering == promptID {
		return answerClaim{}, ErrStale
	}
	c := answerClaim{pane: e.Pane, menu: e.Menu, idx: -1}
	for i, o := range p.Options {
		c.options = append(c.options, o.Label)
		if keyRe.MatchString(key) && o.Key == key {
			c.idx, c.label = i, o.Label
		}
	}
	if c.idx < 0 {
		return answerClaim{}, ErrBadKey
	}
	if !paneRe.MatchString(c.pane) {
		return answerClaim{}, ErrNotInTmux
	}
	e.Answering = promptID // claim it before the lock goes
	return c, nil
}

// menuKeys re-reads the pane and returns the arrow keys from the cursor's
// row to the chosen one, then Enter; ErrStale when the pane no longer shows
// that menu.
func (t *Tracker) menuKeys(ctx context.Context, c answerClaim) ([]string, error) {
	_, rows, cursor, ok := cursorMenu(t.d.Tmux.Capture(ctx, c.pane, 0))
	if !ok || len(rows) != len(c.options) {
		return nil, ErrStale
	}
	for i, l := range c.options {
		if rows[i].Label != l {
			return nil, ErrStale
		}
	}
	var keys []string
	for d := c.idx - cursor; d != 0; {
		if d > 0 {
			keys, d = append(keys, "Down"), d-1
		} else {
			keys, d = append(keys, "Up"), d+1
		}
	}
	return append(keys, "Enter"), nil
}

// releasePrompt gives a claimed prompt back: nothing was typed.
func (t *Tracker) releasePrompt(agentID, promptID string) {
	t.mu.Lock()
	defer t.mu.Unlock()
	if e := t.entries[agentID]; e != nil && e.Answering == promptID {
		e.Answering = ""
	}
}

// answered moves the agent on after a sent answer; true when it changed.
func (t *Tracker) answered(agentID, promptID, label string, now time.Time) bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	e := t.entries[agentID]
	if e == nil {
		return false
	}
	if e.Answering == promptID {
		e.Answering = ""
	}
	if !openPrompt(e, promptID) {
		return false
	}
	switch {
	case e.Menu:
		e.Menu = false
		e.SetStatus(afterMenu(e), now)
	case strings.HasPrefix(label, "no"):
		e.SetStatus(Idle, now) // Claude stops and waits for instructions
	default:
		e.SetStatus(Running, now)
	}
	return true
}

// Snapshot returns copies of every agent, by id.
func (t *Tracker) Snapshot() []Agent {
	t.mu.Lock()
	defer t.mu.Unlock()
	out := make([]Agent, 0, len(t.entries))
	for _, e := range t.entries {
		a := e.Agent
		if a.Activity != nil {
			x := *a.Activity
			a.Activity = &x
		}
		if a.Waiting != nil {
			w := *a.Waiting
			w.Options = slices.Clone(w.Options)
			a.Waiting = &w
		}
		a.Subagents = slices.Clone(a.Subagents)
		out = append(out, a)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out
}

// Subscribe returns a channel that ticks after every change (buffered 1:
// ticks coalesce, the tracker never waits) and a function to stop.
func (t *Tracker) Subscribe() (<-chan struct{}, func()) {
	ch := make(chan struct{}, 1)
	id := t.addSub(ch)
	return ch, func() {
		t.subMu.Lock()
		defer t.subMu.Unlock()
		delete(t.subs, id)
	}
}

func (t *Tracker) addSub(ch chan struct{}) int {
	t.subMu.Lock()
	defer t.subMu.Unlock()
	id := t.nextSub
	t.nextSub++
	t.subs[id] = ch
	return id
}

func (t *Tracker) notify() {
	t.subMu.Lock()
	defer t.subMu.Unlock()
	for _, ch := range t.subs {
		select {
		case ch <- struct{}{}:
		default:
		}
	}
}

// trustedTranscript returns p cleaned when it is a transcript we may read: an
// absolute .jsonl path inside Projects (also with symlinks resolved) naming a
// regular file (no symlink, FIFO or device); else "".
func (t *Tracker) trustedTranscript(p string) string {
	if p == "" || !filepath.IsAbs(p) {
		return ""
	}
	c := filepath.Clean(p)
	if !strings.HasSuffix(c, ".jsonl") || !within(t.d.Projects, c) {
		return ""
	}
	st, err := os.Lstat(c)
	if err != nil || !st.Mode().IsRegular() {
		return ""
	}
	root, err := filepath.EvalSymlinks(t.d.Projects)
	if err != nil {
		return ""
	}
	dir, err := filepath.EvalSymlinks(filepath.Dir(c))
	if err != nil || !within(root, filepath.Join(dir, filepath.Base(c))) {
		return ""
	}
	return c
}

// within reports whether p lies below root.
func within(root, p string) bool {
	rel, err := filepath.Rel(root, p)
	return err == nil && rel != "." && rel != ".." && !strings.HasPrefix(rel, ".."+string(filepath.Separator)) && !filepath.IsAbs(rel)
}
