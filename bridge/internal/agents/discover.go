// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/claude/live.go (ActiveSessions, ttysOf, LiveSessions, isInteractiveClaude, sessionArg, newestTranscript, processCwds, findTranscript, projectName), backend/internal/claude/sessions.go (IsSessionID), backend/internal/daemon/live.go (discover) and backend/internal/daemon/daemon.go (LookPathLogin, shellJoin).

package agents

import (
	"context"
	"encoding/json"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

// Live is a Claude Code session running on this machine.
type Live struct {
	PID        int
	TTY        string // /dev/ttys012; empty for background sessions
	Cwd        string
	SessionID  string
	Transcript string
	Name       string // from `claude agents --json`
	Started    time.Time

	// From `claude agents --json` (false with the process-scan fallback).
	Background bool // a background session (no terminal)
	Busy       bool // working on a turn
	Blocked    bool // waiting for you (permission, question)
	Reported   bool // status came from Claude Code itself
}

// Discoverer lists the running Claude Code sessions. ok is false when the
// discovery failed (nothing can be concluded from it). run identifies the
// discovery run the list comes from: a cached list is served with the run it
// was read in, so a session is "missing" once per run, not once per call.
type Discoverer interface {
	Sessions(ctx context.Context) (lives []Live, run uint64, ok bool)
}

const (
	agentsEvery     = 10 * time.Second // `claude agents --json` at most this often
	agentsIdleEvery = 60 * time.Second // ... once no agent was seen for idleAfter
	idleAfter       = 10 * time.Minute
	lookRetry       = 5 * time.Minute // a failed claude lookup is retried this often
	agentsTimeout   = 15 * time.Second
	lookTimeout     = 10 * time.Second
	scanTimeout     = 5 * time.Second // ps, lsof
)

// Discovery asks Claude Code itself (`claude agents --json`, at most every
// 10 s, every 60 s after 10 minutes without an agent), else scans processes
// (older Claude Code, or no claude on the login PATH yet).
type Discovery struct {
	Run      Runner
	Projects string // transcript folder (~/.claude/projects)
	Now      func() time.Time
	Logf     func(format string, args ...any) // nil = silent
	// Candidates are where claude is looked for when the login shell does
	// not name it (nil = nowhere else; NewDiscovery fills in claudeCandidates).
	Candidates []string

	mu          sync.Mutex
	claudeBin   string    // resolved from the login shell; "" = not (yet) found
	lookFailed  time.Time // the last failed lookup (zero: none)
	reported    []Live
	reportedOK  bool
	reportedAt  time.Time
	reportedRun uint64
	runs        uint64    // discovery runs so far
	lastSeen    time.Time // the last run that listed an agent (or the first run)
	fallback    bool      // the process scan is in use (said once in the log)
}

// nextRun numbers a new discovery run.
func (d *Discovery) nextRun() uint64 {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.runs++
	return d.runs
}

// NewDiscovery is the real discoverer over transcripts in projects.
func NewDiscovery(projects string) *Discovery {
	home, _ := os.UserHomeDir()
	return &Discovery{Run: execRunner, Projects: projects, Now: time.Now, Logf: log.Printf,
		Candidates: claudeCandidates(home)}
}

// claudeCandidates are where Claude Code's installers put claude. A login
// shell (zsh -l) reads ~/.zprofile but not ~/.zshrc, and the native installer
// often adds ~/.local/bin to PATH only in the latter, so under launchd the
// lookup can miss a claude every terminal finds.
func claudeCandidates(home string) []string {
	var c []string
	if home != "" {
		c = append(c, filepath.Join(home, ".local", "bin", "claude"), filepath.Join(home, ".claude", "local", "claude"))
	}
	return append(c, "/opt/homebrew/bin/claude", "/usr/local/bin/claude")
}

// firstExecutable is the first of paths that is an executable file.
func firstExecutable(paths []string) string {
	for _, p := range paths {
		if st, err := os.Stat(p); err == nil && st.Mode().IsRegular() && st.Mode().Perm()&0o111 != 0 {
			return p
		}
	}
	return ""
}

func (d *Discovery) Sessions(ctx context.Context) ([]Live, uint64, bool) {
	now := d.Now()
	bin := d.claude(ctx, now)
	cached, ok, run, fresh := d.cached(now)
	if !fresh && bin != "" {
		cached, ok = activeSessions(ctx, d.Run, bin, d.Projects)
		run = d.nextRun()
		d.store(cached, ok, run)
	}
	if ok {
		d.useFallback(false, "")
		return append([]Live{}, cached...), run, true
	}
	why := "`claude agents --json` failed"
	if bin == "" {
		why = "no claude on the login shell's PATH"
	}
	d.useFallback(true, why)
	lives := liveSessions(ctx, d.Run, d.Projects)
	if len(lives) > 0 {
		d.seen(d.Now())
	}
	return lives, d.nextRun(), lives != nil
}

// Claude is the claude binary, found as for discovery ("" = not found).
func (d *Discovery) Claude(ctx context.Context) string { return d.claude(ctx, d.Now()) }

// claude is the claude binary, looked up through the login shell, else in
// d.Candidates; after a failed lookup it is looked up again at most every
// lookRetry.
func (d *Discovery) claude(ctx context.Context, now time.Time) string {
	if bin, look := d.needLookup(now); !look {
		return bin
	}
	bin := lookPathLogin(ctx, d.Run, "claude")
	if bin == "" {
		bin = firstExecutable(d.Candidates)
	}
	return d.looked(bin, now)
}

func (d *Discovery) needLookup(now time.Time) (bin string, look bool) {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.claudeBin, d.claudeBin == "" && (d.lookFailed.IsZero() || now.Sub(d.lookFailed) >= lookRetry)
}

func (d *Discovery) looked(bin string, now time.Time) string {
	d.mu.Lock()
	defer d.mu.Unlock()
	if bin == "" {
		d.lookFailed = now
	} else if d.claudeBin == "" {
		d.claudeBin = bin
	}
	return d.claudeBin
}

// cached is the last `claude agents` answer, fresh when younger than the
// interval (agentsEvery, or agentsIdleEvery once no agent was seen for a while).
func (d *Discovery) cached(now time.Time) (lives []Live, ok bool, run uint64, fresh bool) {
	d.mu.Lock()
	defer d.mu.Unlock()
	if d.lastSeen.IsZero() {
		d.lastSeen = now
	}
	every := agentsEvery
	if now.Sub(d.lastSeen) >= idleAfter {
		every = agentsIdleEvery
	}
	fresh = !d.reportedAt.IsZero() && now.Sub(d.reportedAt) < every
	return d.reported, d.reportedOK, d.reportedRun, fresh
}

// store keeps one `claude agents` answer.
func (d *Discovery) store(lives []Live, ok bool, run uint64) {
	d.mu.Lock()
	defer d.mu.Unlock()
	now := d.Now()
	d.reported, d.reportedOK, d.reportedAt, d.reportedRun = lives, ok, now, run
	if ok && len(lives) > 0 {
		d.lastSeen = now
	}
}

func (d *Discovery) seen(now time.Time) {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.lastSeen = now
}

// useFallback logs once when discovery falls back to the process scan, and
// once when `claude agents` answers again.
func (d *Discovery) useFallback(on bool, why string) {
	d.mu.Lock()
	defer d.mu.Unlock()
	if on == d.fallback {
		return
	}
	d.fallback = on
	if d.Logf == nil {
		return
	}
	if on {
		d.Logf("agents: %s; using the process scan", why)
	} else {
		d.Logf("agents: claude agents answers again")
	}
}

// claudeAgent is one entry of `claude agents --json`.
type claudeAgent struct {
	ID        string `json:"id"` // short id, background sessions only
	Cwd       string `json:"cwd"`
	Kind      string `json:"kind"` // interactive | background
	StartedAt int64  `json:"startedAt"`
	SessionID string `json:"sessionId"`
	Name      string `json:"name"`
	State     string `json:"state"` // background: blocked | done | …
	PID       int    `json:"pid"`
	Status    string `json:"status"` // idle | busy
}

// activeSessions asks Claude Code itself for every active session,
// interactive and background. ok is false when the claude CLI can't answer
// (missing, too old): then use liveSessions.
func activeSessions(ctx context.Context, run Runner, claudeBin, projects string) (out []Live, ok bool) {
	ctx, cancel := context.WithTimeout(ctx, agentsTimeout)
	defer cancel()
	raw, err := run(ctx, claudeBin, "agents", "--json")
	if err != nil {
		return nil, false
	}
	var list []claudeAgent
	if json.Unmarshal(raw, &list) != nil {
		return nil, false
	}
	ttys := ttysOf(ctx, run, list)
	out = []Live{}
	for _, a := range list {
		if !IsSessionID(a.SessionID) || a.Cwd == "" {
			continue
		}
		l := Live{PID: a.PID, TTY: ttys[a.PID], Cwd: a.Cwd, SessionID: a.SessionID,
			Name: a.Name, Background: a.Kind == "background",
			Busy: a.Status == "busy", Blocked: a.State == "blocked", Reported: true}
		if a.StartedAt > 0 {
			l.Started = time.UnixMilli(a.StartedAt)
		}
		l.Transcript = findTranscript(projects, a.Cwd, a.SessionID)
		out = append(out, l)
	}
	if len(list) > 0 && len(out) == 0 {
		return nil, false // entries but none usable: the format changed, not "nothing running"
	}
	return out, true
}

// ttysOf maps the interactive sessions' processes to their terminals.
func ttysOf(ctx context.Context, run Runner, list []claudeAgent) map[int]string {
	var pids []string
	for _, a := range list {
		if a.PID > 0 && a.Kind != "background" {
			pids = append(pids, strconv.Itoa(a.PID))
		}
	}
	ttys := map[int]string{}
	if len(pids) == 0 {
		return ttys
	}
	ctx, cancel := context.WithTimeout(ctx, scanTimeout)
	defer cancel()
	out, _ := run(ctx, "/bin/ps", "-o", "pid=,tty=", "-p", strings.Join(pids, ","))
	for _, l := range strings.Split(string(out), "\n") {
		f := strings.Fields(l)
		if len(f) == 2 && f[1] != "??" {
			if pid, err := strconv.Atoi(f[0]); err == nil {
				ttys[pid] = "/dev/" + f[1]
			}
		}
	}
	return ttys
}

// findTranscript is a session's transcript: in its folder's project
// directory, else wherever it was written (a worktree, a moved folder), else
// where it will be written.
func findTranscript(projects, cwd, sessionID string) string {
	p := filepath.Join(projects, projectName(cwd), sessionID+".jsonl")
	if _, err := os.Stat(p); err == nil {
		return p
	}
	if m, _ := filepath.Glob(filepath.Join(projects, "*", sessionID+".jsonl")); len(m) > 0 {
		return m[0]
	}
	return p
}

// projectName is Claude's transcript folder name for a working directory:
// the path with every character other than a letter or digit turned into "-".
func projectName(cwd string) string { return nonAlnum.ReplaceAllString(cwd, "-") }

var nonAlnum = regexp.MustCompile(`[^A-Za-z0-9]`)

var uuidRe = regexp.MustCompile(`^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$`)

// IsSessionID reports whether s looks like a Claude Code session id.
func IsSessionID(s string) bool { return uuidRe.MatchString(s) }

// liveSessions lists interactive `claude` processes that have a terminal,
// each matched to its session transcript. Processes whose transcript can't
// be found are left out. nil when ps fails.
func liveSessions(ctx context.Context, run Runner, projects string) []Live {
	ctx, cancel := context.WithTimeout(ctx, scanTimeout)
	defer cancel()
	out, err := run(ctx, "/bin/ps", "-axww", "-o", "pid=,lstart=,tty=,args=")
	if err != nil {
		return nil
	}
	var procs []Live
	args := map[int][]string{}
	for _, line := range strings.Split(string(out), "\n") {
		f := strings.Fields(line)
		if len(f) < 8 { // pid, 5 lstart fields, tty, argv0
			continue
		}
		pid, err := strconv.Atoi(f[0])
		if err != nil {
			continue
		}
		tty, argv := f[6], f[7:]
		if tty == "??" || tty == "-" || !isInteractiveClaude(argv) {
			continue
		}
		started, _ := time.ParseInLocation("Mon Jan 2 15:04:05 2006", strings.Join(f[1:6], " "), time.Local)
		procs = append(procs, Live{PID: pid, TTY: "/dev/" + tty, Started: started})
		args[pid] = argv
	}
	res := []Live{}
	if len(procs) == 0 {
		return res
	}
	cwds := processCwds(ctx, run, procs)
	claimed := map[string]bool{}
	// Explicit session ids first, then the oldest processes pick first.
	sort.Slice(procs, func(i, j int) bool { return procs[i].Started.Before(procs[j].Started) })
	for _, explicitPass := range []bool{true, false} {
		for i := range procs {
			p := &procs[i]
			if p.SessionID != "" {
				continue
			}
			p.Cwd = cwds[p.PID]
			if p.Cwd == "" {
				continue
			}
			id := sessionArg(args[p.PID])
			if explicitPass != (id != "") {
				continue
			}
			dir := filepath.Join(projects, projectName(p.Cwd))
			if id == "" {
				id = newestTranscript(dir, p.Started, claimed)
			}
			if id == "" || claimed[id] {
				continue
			}
			claimed[id] = true
			p.SessionID = id
			p.Transcript = filepath.Join(dir, id+".jsonl")
			res = append(res, *p)
		}
	}
	sort.Slice(res, func(i, j int) bool { return res[i].Started.Before(res[j].Started) })
	return res
}

// isInteractiveClaude: argv of Claude Code's interactive CLI, not one of its
// helpers (background pty hosts, spares, the daemon) or a one-shot -p run.
func isInteractiveClaude(argv []string) bool {
	if len(argv) == 0 {
		return false
	}
	base := filepath.Base(argv[0])
	if base != "claude" && base != "claude.exe" {
		return false
	}
	for _, a := range argv[1:] {
		switch a {
		case "-p", "--print", "bg-pty-host", "--bg-pty-host", "bg-spare", "--bg-spare", "daemon", "mcp", "--version", "-v", "update", "doctor":
			return false
		}
	}
	return true
}

// sessionArg is the session id given on the command line, if any.
func sessionArg(argv []string) string {
	for i, a := range argv {
		for _, flag := range []string{"--resume", "-r", "--session-id"} {
			if a == flag && i+1 < len(argv) && IsSessionID(argv[i+1]) {
				return argv[i+1]
			}
			if v, ok := strings.CutPrefix(a, flag+"="); ok && IsSessionID(v) {
				return v
			}
		}
	}
	return ""
}

// newestTranscript is the session in dir most recently written since the
// process started (a fresh or resumed session writes as soon as it runs).
func newestTranscript(dir string, since time.Time, claimed map[string]bool) string {
	files, _ := filepath.Glob(filepath.Join(dir, "*.jsonl"))
	best, bestT := "", time.Time{}
	for _, f := range files {
		id := strings.TrimSuffix(filepath.Base(f), ".jsonl")
		if !IsSessionID(id) || claimed[id] {
			continue
		}
		st, err := os.Stat(f)
		if err != nil || st.ModTime().Before(since.Add(-time.Minute)) {
			continue
		}
		if st.ModTime().After(bestT) {
			best, bestT = id, st.ModTime()
		}
	}
	return best
}

// processCwds asks lsof for the working directories of the given processes.
func processCwds(ctx context.Context, run Runner, procs []Live) map[int]string {
	pids := make([]string, len(procs))
	for i, p := range procs {
		pids[i] = strconv.Itoa(p.PID)
	}
	out, _ := run(ctx, "/usr/sbin/lsof", "-a", "-d", "cwd", "-p", strings.Join(pids, ","), "-Fpn")
	cwds := map[int]string{}
	pid := 0
	for _, l := range strings.Split(string(out), "\n") {
		switch {
		case strings.HasPrefix(l, "p"):
			pid, _ = strconv.Atoi(l[1:])
		case strings.HasPrefix(l, "n") && pid != 0:
			cwds[pid] = l[1:]
		}
	}
	return cwds
}

// lookPathLogin resolves a command through the user's login shell PATH.
func lookPathLogin(ctx context.Context, run Runner, name string) string {
	ctx, cancel := context.WithTimeout(ctx, lookTimeout)
	defer cancel()
	out, err := run(ctx, "/bin/zsh", "-lc", "command -v "+shellJoin([]string{name}))
	if err != nil {
		return ""
	}
	p := strings.TrimSpace(string(out))
	if !filepath.IsAbs(p) {
		return "" // an alias or a function, not a binary
	}
	return p
}

func shellJoin(argv []string) string {
	q := make([]string, len(argv))
	for i, a := range argv {
		q[i] = "'" + strings.ReplaceAll(a, "'", `'\''`) + "'"
	}
	return strings.Join(q, " ")
}
