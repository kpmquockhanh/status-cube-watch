// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/claude/live_test.go and backend/internal/daemon/live_test.go (TestClaudeAgentsList).

package agents

import (
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

const (
	sidA = "0b5c2f4e-1111-4222-8333-944455556666"
	sidB = "5d6e7f80-1111-4222-8333-944455556666"
	sidC = "9a8b7c6d-1111-4222-8333-944455556666"
)

func TestProjectName(t *testing.T) {
	if got := projectName("/Users/sam/dev/my.app"); got != "-Users-sam-dev-my-app" {
		t.Fatal(got)
	}
}

func TestInteractiveClaude(t *testing.T) {
	for argv, want := range map[string]bool{
		"claude":                              true,
		"/opt/homebrew/bin/claude --resume x": true,
		"claude.exe":                          true,
		"claude -p hello":                     false,
		"claude bg-pty-host --bg-pty-host /tmp/s":  false,
		"claude bg-spare --bg-spare /tmp/s":        false,
		"claude.exe daemon run --origin transient": false,
		"node server.mjs":                          false,
	} {
		if got := isInteractiveClaude(strings.Fields(argv)); got != want {
			t.Errorf("%q: %v", argv, got)
		}
	}
	if sessionArg(strings.Fields("claude --resume "+sidA)) != sidA || sessionArg(strings.Fields("claude --session-id="+sidA)) != sidA || sessionArg(strings.Fields("claude -c")) != "" {
		t.Fatal("session args")
	}
}

func TestIsSessionID(t *testing.T) {
	if !IsSessionID(sidA) || IsSessionID("not-a-session") || IsSessionID("../"+sidA) {
		t.Fatal("session ids")
	}
}

func agentsJSON() string {
	return `[
 {"cwd":"/work/app","kind":"interactive","startedAt":1791446002609,"sessionId":"` + sidA + `","pid":101,"status":"busy"},
 {"id":"ab12cd34","cwd":"/work/api","kind":"background","startedAt":1791446002609,"sessionId":"` + sidB + `","name":"refactor auth","state":"blocked","pid":102,"status":"idle"},
 {"cwd":"/work/bad","kind":"interactive","sessionId":"nope","pid":103}
]`
}

func TestDiscoveryClaudeAgents(t *testing.T) {
	projects := t.TempDir()
	dir := filepath.Join(projects, "-work-app")
	if err := os.MkdirAll(dir, 0o700); err != nil {
		t.Fatal(err)
	}
	tr := filepath.Join(dir, sidA+".jsonl")
	if err := os.WriteFile(tr, []byte("{}\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	f := newFakeRun()
	f.set("/bin/zsh -lc command -v 'claude'", "/usr/local/bin/claude\n")
	f.set("/usr/local/bin/claude agents --json", agentsJSON())
	f.set("/bin/ps -o pid=,tty= -p", "  101 ttys012\n")
	now := time.Unix(1_800_000_000, 0)
	d := &Discovery{Run: f.run, Projects: projects, Now: func() time.Time { return now }}

	got, gen1, ok := d.Sessions(context.Background())
	if !ok || len(got) != 2 {
		t.Fatalf("want 2 sessions (bad id dropped), got %v %+v", ok, got)
	}
	a, b := got[0], got[1]
	if a.SessionID != sidA || a.TTY != "/dev/ttys012" || a.Cwd != "/work/app" || !a.Busy || a.Background || !a.Reported || a.Transcript != tr || a.PID != 101 {
		t.Fatalf("interactive: %+v", a)
	}
	if b.SessionID != sidB || b.TTY != "" || !b.Background || !b.Blocked || b.Name != "refactor auth" || b.Started.UnixMilli() != 1791446002609 {
		t.Fatalf("background: %+v", b)
	}
	if b.Transcript != filepath.Join(projects, "-work-api", sidB+".jsonl") {
		t.Fatalf("missing transcript falls back to the default path: %q", b.Transcript)
	}

	// Within 10 s the list is reused (same run); the login shell is asked only once.
	if _, gen2, ok := d.Sessions(context.Background()); !ok || gen2 != gen1 {
		t.Fatalf("a cached run is the same run: %d vs %d", gen2, gen1)
	}
	now = now.Add(11 * time.Second)
	if _, gen3, ok := d.Sessions(context.Background()); !ok || gen3 == gen1 {
		t.Fatal("a fresh run is a new run")
	}
	if n := len(f.matching("/usr/local/bin/claude agents")); n != 2 {
		t.Fatalf("claude agents ran %d times, want 2", n)
	}
	if n := len(f.matching("/bin/zsh")); n != 1 {
		t.Fatalf("lookup ran %d times", n)
	}
}

func TestFindTranscriptElsewhere(t *testing.T) {
	projects := t.TempDir()
	dir := filepath.Join(projects, "-moved")
	_ = os.MkdirAll(dir, 0o700)
	p := filepath.Join(dir, sidA+".jsonl")
	_ = os.WriteFile(p, nil, 0o600)
	if got := findTranscript(projects, "/work/app", sidA); got != p {
		t.Fatalf("got %q", got)
	}
}

func psFixture() string {
	return strings.Join([]string{
		"  201 Thu Oct 22 10:00:00 2026     ttys001 /opt/homebrew/bin/claude --resume " + sidC,
		"  202 Thu Oct 22 10:00:05 2026     ttys002 claude",
		"  203 Thu Oct 22 10:00:06 2026     ttys003 claude -p hi",
		"  204 Thu Oct 22 10:00:07 2026     ??      claude",
		"  205 Thu Oct 22 10:00:08 2026     ttys005 node server.mjs",
		"",
	}, "\n")
}

func TestDiscoveryProcessScan(t *testing.T) {
	projects := t.TempDir()
	dirB := filepath.Join(projects, "-work-b")
	_ = os.MkdirAll(dirB, 0o700)
	newer := filepath.Join(dirB, sidB+".jsonl")
	older := filepath.Join(dirB, sidA+".jsonl")
	_ = os.WriteFile(newer, []byte("{}\n"), 0o600)
	_ = os.WriteFile(older, []byte("{}\n"), 0o600)
	old := time.Date(2026, 10, 22, 9, 0, 0, 0, time.Local) // before the process started
	_ = os.Chtimes(older, old, old)

	f := newFakeRun()
	f.fail["/bin/zsh -lc command -v 'claude'"] = true // no claude on PATH: process scan
	f.set("/bin/ps -axww -o pid=,lstart=,tty=,args=", psFixture())
	f.set("/usr/sbin/lsof -a -d cwd -p", "p201\nfcwd\nn/work/a\np202\nfcwd\nn/work/b\n")
	d := &Discovery{Run: f.run, Projects: projects, Now: time.Now}

	got, gen1, ok := d.Sessions(context.Background())
	if !ok || len(got) != 2 {
		t.Fatalf("got %v %+v", ok, got)
	}
	a, b := got[0], got[1]
	if a.PID != 201 || a.SessionID != sidC || a.TTY != "/dev/ttys001" || a.Cwd != "/work/a" || a.Reported {
		t.Fatalf("explicit --resume: %+v", a)
	}
	if a.Transcript != filepath.Join(projects, "-work-a", sidC+".jsonl") {
		t.Fatalf("transcript %q", a.Transcript)
	}
	if b.PID != 202 || b.SessionID != sidB || b.Transcript != newer {
		t.Fatalf("newest transcript since start: %+v", b)
	}
	if !a.Started.Equal(time.Date(2026, 10, 22, 10, 0, 0, 0, time.Local)) {
		t.Fatalf("started %v", a.Started)
	}
	if len(f.matching("/bin/zsh")) != 1 {
		t.Fatal("lookup once")
	}
	if _, gen2, _ := d.Sessions(context.Background()); gen2 == gen1 {
		t.Fatal("every process scan is a new run")
	}
	if len(f.matching("/bin/zsh")) != 1 {
		t.Fatal("a missing claude is not looked up again")
	}
}

// Fix 4: a `claude agents --json` whose entries no longer decode (renamed
// fields) is not "nothing running": the process scan runs instead.
func TestDiscoveryAgentsRenamedFields(t *testing.T) {
	f := newFakeRun()
	f.set("/bin/zsh -lc command -v 'claude'", "/usr/local/bin/claude\n")
	f.set("/usr/local/bin/claude agents --json", `[{"session_id":"`+sidA+`","workdir":"/work/app","pid":101},{"session_id":"`+sidB+`","workdir":"/work/api"}]`)
	f.set("/bin/ps -o pid=,tty= -p", "")
	f.set("/bin/ps -axww -o pid=,lstart=,tty=,args=", "  300 Thu Oct 22 10:00:00 2026 ttys009 vim\n")
	d := &Discovery{Run: f.run, Projects: t.TempDir(), Now: time.Now}
	if _, _, ok := d.Sessions(context.Background()); !ok {
		t.Fatal("the process scan answered")
	}
	if len(f.matching("/bin/ps -axww")) != 1 {
		t.Fatal("process scan not used")
	}
	// An empty list is a real answer: nothing running, no process scan.
	f.set("/usr/local/bin/claude agents --json", `[]`)
	d = &Discovery{Run: f.run, Projects: t.TempDir(), Now: time.Now}
	if got, _, ok := d.Sessions(context.Background()); !ok || len(got) != 0 || len(f.matching("/bin/ps -axww")) != 1 {
		t.Fatalf("empty list: %v %+v", ok, got)
	}
}

func TestDiscoveryAgentsFailFallsBack(t *testing.T) {
	f := newFakeRun()
	f.set("/bin/zsh -lc command -v 'claude'", "/usr/local/bin/claude\n")
	f.fail["/usr/local/bin/claude agents --json"] = true // too old: no `agents`
	f.set("/bin/ps -axww -o pid=,lstart=,tty=,args=", "  300 Thu Oct 22 10:00:00 2026 ttys009 vim\n")
	d := &Discovery{Run: f.run, Projects: t.TempDir(), Now: time.Now}
	got, _, ok := d.Sessions(context.Background())
	if !ok || len(got) != 0 {
		t.Fatalf("a scan that finds nothing is a successful empty list: %v %#v", ok, got)
	}
	if len(f.matching("/bin/ps -axww")) != 1 {
		t.Fatal("process scan not used")
	}
}

func TestDiscoveryFailure(t *testing.T) {
	f := newFakeRun() // everything fails: ps too
	d := &Discovery{Run: f.run, Projects: t.TempDir(), Now: time.Now}
	if got, _, ok := d.Sessions(context.Background()); ok || got != nil {
		t.Fatalf("a failed discovery: %v %#v", ok, got)
	}
}
