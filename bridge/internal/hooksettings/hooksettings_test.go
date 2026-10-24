package hooksettings

import (
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

// fixture is the shape of a real user's settings.json.
const fixture = `{"model":"opus","hooks":{"PreToolUse":[{"matcher":"*","hooks":[{"type":"command","command":"jq -r .cwd | auto-allow"}]}],"Stop":[{"hooks":[{"type":"command","command":"/Users/u/.claude/hooks/notify-finished.py"}]}],"Notification":[{"matcher":"permission_prompt","hooks":[{"type":"command","command":"osascript a"}]},{"matcher":"agent_needs_input","hooks":[{"type":"command","command":"osascript b"}]}]},"permissions":{"allow":["Bash(ls:*)"]},"zzz":1}`

var userCommands = []string{"jq -r .cwd | auto-allow", "/Users/u/.claude/hooks/notify-finished.py", "osascript a", "osascript b"}

const bin = "/Applications/ClaudeCubeLink.app/Contents/Resources/bridge/cube-bridge"

func writeFixture(t *testing.T, body string, mode os.FileMode) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), "settings.json")
	if err := os.WriteFile(p, []byte(body), mode); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(p, mode); err != nil { // umask-proof
		t.Fatal(err)
	}
	return p
}

func read(t *testing.T, p string) []byte {
	t.Helper()
	b, err := os.ReadFile(p)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

// tokens flattens a JSON document into its token stream: equal streams mean
// semantically equal values with the same key order at every level.
func tokens(t *testing.T, b []byte) []any {
	t.Helper()
	dec := json.NewDecoder(bytes.NewReader(b))
	dec.UseNumber()
	var out []any
	for {
		tok, err := dec.Token()
		if errors.Is(err, io.EOF) {
			return out
		}
		if err != nil {
			t.Fatalf("tokens: %v in %s", err, b)
		}
		out = append(out, tok)
	}
}

func sameJSON(t *testing.T, a, b []byte) bool {
	return reflect.DeepEqual(tokens(t, a), tokens(t, b))
}

// view decodes settings in order: top-level keys, and hooks as event -> raw groups.
type view struct {
	keys   []string
	events []string
	groups map[string][]json.RawMessage
}

func load(t *testing.T, b []byte) view {
	t.Helper()
	top, err := parseObject(b)
	if err != nil {
		t.Fatalf("parse: %v", err)
	}
	v := view{groups: map[string][]json.RawMessage{}}
	for _, f := range top {
		v.keys = append(v.keys, f.Key)
		if f.Key != "hooks" {
			continue
		}
		hooks, err := parseObject(f.Value)
		if err != nil {
			t.Fatal(err)
		}
		for _, e := range hooks {
			v.events = append(v.events, e.Key)
			var arr []json.RawMessage
			if err := json.Unmarshal(e.Value, &arr); err != nil {
				t.Fatal(err)
			}
			v.groups[e.Key] = arr
		}
	}
	return v
}

type group struct {
	Matcher *string `json:"matcher"`
	Hooks   []struct {
		Type    string `json:"type"`
		Command string `json:"command"`
		Timeout int    `json:"timeout"`
	} `json:"hooks"`
}

func ours(t *testing.T, raw json.RawMessage, cmd string) bool {
	var g group
	if err := json.Unmarshal(raw, &g); err != nil {
		t.Fatal(err)
	}
	return len(g.Hooks) == 1 && g.Hooks[0].Command == cmd && g.Hooks[0].Type == "command" && g.Hooks[0].Timeout == 5
}

func TestIsOurs(t *testing.T) {
	if !IsOurs(Command(bin, 8787)) || !IsOurs(Command("/x/cube-bridge", 1)) {
		t.Fatal("our command not recognised")
	}
	for _, c := range append(userCommands, "cube-bridge serve", "agentctl hook --port 1", "") {
		if IsOurs(c) {
			t.Fatalf("user command matched: %q", c)
		}
	}
}

func TestIsOursAnyBinaryName(t *testing.T) {
	for _, b := range []string{"/usr/local/bin/cubebridge", "/Users/u/it's a dir/my bridge"} {
		cmd := Command(b, 8787)
		if !IsOurs(cmd) {
			t.Fatalf("not ours: %q", cmd)
		}
		p := writeFixture(t, fixture, 0o644)
		for _, port := range []int{8787, 9000} {
			if err := Install(p, Command(b, port)); err != nil {
				t.Fatal(err)
			}
		}
		got := load(t, read(t, p))
		for _, ev := range Events {
			n := 0
			for _, g := range got.groups[ev] {
				if ours(t, g, Command(b, 9000)) {
					n++
				}
			}
			if len(got.groups[ev]) != len(load(t, []byte(fixture)).groups[ev])+1 || n != 1 {
				t.Fatalf("%s: %s", ev, got.groups[ev])
			}
		}
		if err := Uninstall(p); err != nil {
			t.Fatal(err)
		}
		if b := read(t, p); !sameJSON(t, b, []byte(fixture)) {
			t.Fatalf("not restored:\n%s", b)
		}
	}
	// near misses: different paths in the two places, extra text, no port
	for _, c := range []string{
		"[ -x '/a' ] && '/b' hook --port 1 >/dev/null 2>&1; exit 0",
		Command("/a", 1) + "; rm -rf x",
		"echo; " + Command("/a", 1),
		"[ -x '/a' ] && '/a' hook --port  >/dev/null 2>&1; exit 0",
	} {
		if IsOurs(c) {
			t.Fatalf("matched: %q", c)
		}
	}
}

func TestInstallRefusesForeignCommand(t *testing.T) {
	p := writeFixture(t, fixture, 0o644)
	if err := Install(p, "echo hi"); err == nil {
		t.Fatal("no error")
	}
	if b := read(t, p); string(b) != fixture {
		t.Fatalf("written: %s", b)
	}
}

func TestDuplicateKeysRefused(t *testing.T) {
	cmd := Command(bin, 8787)
	for _, body := range []string{
		`{"hooks":{"Stop":[]},"x":1,"hooks":{"Stop":[]}}`,
		`{"model":"a","model":"b"}`,
		`{"hooks":{"Stop":[],"Stop":[{"hooks":[]}]}}`,
		`{"hooks":{"Stop":[{"hooks":[],"hooks":[{"type":"command","command":` + quote(cmd) + `}]}]}}`,
	} {
		p := writeFixture(t, body, 0o644)
		for name, fn := range map[string]func() error{
			"install":   func() error { return Install(p, cmd) },
			"uninstall": func() error { return Uninstall(p) },
		} {
			err := fn()
			if err == nil || !strings.Contains(err.Error(), "duplicate key") || !strings.Contains(err.Error(), p) {
				t.Fatalf("%s %s: %v", name, body, err)
			}
		}
		if b := read(t, p); string(b) != body {
			t.Fatalf("%s: written %s", body, b)
		}
		entries, _ := os.ReadDir(filepath.Dir(p))
		if len(entries) != 1 {
			t.Fatalf("%s: stray files %v", body, entries)
		}
	}
}

func TestInstallKeepsUserHooks(t *testing.T) {
	p := writeFixture(t, fixture, 0o644)
	cmd := Command(bin, 8787)
	if err := Install(p, cmd); err != nil {
		t.Fatal(err)
	}
	got := load(t, read(t, p))
	want := load(t, []byte(fixture))
	if !reflect.DeepEqual(got.keys, []string{"model", "hooks", "permissions", "zzz"}) {
		t.Fatalf("top-level keys: %v", got.keys)
	}
	if !reflect.DeepEqual(got.events[:3], want.events) {
		t.Fatalf("user events moved: %v", got.events)
	}
	for _, ev := range Events {
		user := want.groups[ev]
		gs := got.groups[ev]
		if len(gs) != len(user)+1 {
			t.Fatalf("%s: %d groups", ev, len(gs))
		}
		for i, u := range user {
			if !sameJSON(t, u, gs[i]) {
				t.Fatalf("%s group %d changed: %s", ev, i, gs[i])
			}
		}
		last := gs[len(gs)-1]
		if !ours(t, last, cmd) {
			t.Fatalf("%s: last group not ours: %s", ev, last)
		}
		var g group
		_ = json.Unmarshal(last, &g)
		wantMatcher := ev == "PreToolUse" || ev == "PostToolUse" || ev == "PermissionRequest"
		if (g.Matcher != nil) != wantMatcher || (g.Matcher != nil && *g.Matcher != "*") {
			t.Fatalf("%s matcher: %v", ev, g.Matcher)
		}
	}
	if len(got.events) != 9 {
		t.Fatalf("events: %v", got.events)
	}
	// the command is written as-is, without HTML escaping
	if !bytes.Contains(read(t, p), []byte(`&& '`)) {
		t.Fatalf("command escaped:\n%s", read(t, p))
	}
}

func TestInstallIdempotent(t *testing.T) {
	p := writeFixture(t, fixture, 0o644)
	for _, port := range []int{8787, 8787, 9000} {
		if err := Install(p, Command(bin, port)); err != nil {
			t.Fatal(err)
		}
	}
	cmd := Command(bin, 9000)
	got := load(t, read(t, p))
	for _, ev := range Events {
		n := 0
		for _, g := range got.groups[ev] {
			var gg group
			_ = json.Unmarshal(g, &gg)
			for _, h := range gg.Hooks {
				if IsOurs(h.Command) {
					n++
					if h.Command != cmd {
						t.Fatalf("%s: stale command %q", ev, h.Command)
					}
				}
			}
		}
		if n != 1 {
			t.Fatalf("%s: %d of our hooks", ev, n)
		}
	}
}

func TestUninstallRestores(t *testing.T) {
	p := writeFixture(t, fixture, 0o644)
	if err := Install(p, Command(bin, 8787)); err != nil {
		t.Fatal(err)
	}
	if err := Uninstall(p); err != nil {
		t.Fatal(err)
	}
	if b := read(t, p); !sameJSON(t, b, []byte(fixture)) {
		t.Fatalf("not restored:\n%s", b)
	}
}

func TestUninstallKeepsUserHookInSharedGroup(t *testing.T) {
	cmd := Command(bin, 8787)
	body := `{"hooks":{"Stop":[{"hooks":[{"type":"command","command":"say hi"},{"type":"command","command":` + quote(cmd) + `}]}]}}`
	p := writeFixture(t, body, 0o644)
	if err := Uninstall(p); err != nil {
		t.Fatal(err)
	}
	want := `{"hooks":{"Stop":[{"hooks":[{"type":"command","command":"say hi"}]}]}}`
	if b := read(t, p); !sameJSON(t, b, []byte(want)) {
		t.Fatalf("got %s", b)
	}
}

func TestUninstallWithoutOursLeavesFileAlone(t *testing.T) {
	body := "{\"model\": \"opus\",   \"zzz\":1}"
	p := writeFixture(t, body, 0o644)
	if err := Uninstall(p); err != nil {
		t.Fatal(err)
	}
	if b := read(t, p); string(b) != body {
		t.Fatalf("rewritten: %q", b)
	}
	if _, err := os.Stat(p + ".cube-bridge.bak"); !os.IsNotExist(err) {
		t.Fatalf("backup written: %v", err)
	}
}

func quote(s string) string {
	var b bytes.Buffer
	enc := json.NewEncoder(&b)
	enc.SetEscapeHTML(false)
	_ = enc.Encode(s)
	return strings.TrimSpace(b.String())
}

func TestUserStringsSurvive(t *testing.T) {
	body := `{"env":{"A":"<tag> & \"q\" \\ é ☃ é"},"hooks":{}}`
	p := writeFixture(t, body, 0o644)
	if err := Install(p, Command(bin, 8787)); err != nil {
		t.Fatal(err)
	}
	if err := Uninstall(p); err != nil {
		t.Fatal(err)
	}
	b := read(t, p)
	if !bytes.Contains(b, []byte(`"<tag> & \"q\" \\ é ☃ é"`)) {
		t.Fatalf("string bytes changed:\n%s", b)
	}
}

func TestBrokenJSON(t *testing.T) {
	for _, body := range []string{`{"hooks": `, ``, `[]`, `{"hooks":[]}`, `{"hooks":{"Stop":{}}}`, `{} x`, `"s"`} {
		p := writeFixture(t, body, 0o644)
		err := Install(p, Command(bin, 8787))
		if err == nil || !strings.Contains(err.Error(), p) {
			t.Fatalf("%q: install err %v", body, err)
		}
		if body != `{"hooks": ` && body != `` {
			if err := Uninstall(p); err == nil || !strings.Contains(err.Error(), p) {
				t.Fatalf("%q: uninstall err %v", body, err)
			}
		}
		if b := read(t, p); string(b) != body {
			t.Fatalf("%q: file changed to %q", body, b)
		}
		entries, _ := os.ReadDir(filepath.Dir(p))
		if len(entries) != 1 {
			t.Fatalf("%q: stray files %v", body, entries)
		}
	}
}

func TestMissingFile(t *testing.T) {
	p := filepath.Join(t.TempDir(), "sub", "settings.json")
	if err := Uninstall(p); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(p); !os.IsNotExist(err) {
		t.Fatalf("uninstall created the file: %v", err)
	}
	cmd := Command(bin, 8787)
	if err := Install(p, cmd); err != nil {
		t.Fatal(err)
	}
	got := load(t, read(t, p))
	if !reflect.DeepEqual(got.keys, []string{"hooks"}) || !reflect.DeepEqual(got.events, Events) {
		t.Fatalf("keys %v events %v", got.keys, got.events)
	}
	for _, ev := range Events {
		if len(got.groups[ev]) != 1 || !ours(t, got.groups[ev][0], cmd) {
			t.Fatalf("%s: %s", ev, got.groups[ev])
		}
	}
	if _, err := os.Stat(p + ".cube-bridge.bak"); !os.IsNotExist(err) {
		t.Fatalf("backup of a missing file: %v", err)
	}
	if err := Uninstall(p); err != nil {
		t.Fatal(err)
	}
	if b := read(t, p); !sameJSON(t, b, []byte(`{}`)) {
		t.Fatalf("after uninstall: %s", b)
	}
}

func TestBackupAndAtomic(t *testing.T) {
	for _, mode := range []os.FileMode{0o600, 0o644} {
		p := writeFixture(t, fixture, mode)
		if err := Install(p, Command(bin, 8787)); err != nil {
			t.Fatal(err)
		}
		if err := Install(p, Command(bin, 9000)); err != nil {
			t.Fatal(err)
		}
		if err := Uninstall(p); err != nil {
			t.Fatal(err)
		}
		if b := read(t, p+".cube-bridge.bak"); string(b) != fixture {
			t.Fatalf("backup: %s", b)
		}
		for _, f := range []string{p, p + ".cube-bridge.bak"} {
			st, err := os.Stat(f)
			if err != nil || st.Mode().Perm() != mode {
				t.Fatalf("%s mode %v want %v (%v)", f, st.Mode().Perm(), mode, err)
			}
		}
		entries, _ := os.ReadDir(filepath.Dir(p))
		if len(entries) != 2 {
			t.Fatalf("stray files: %v", entries)
		}
		if b := read(t, p); !bytes.HasSuffix(b, []byte("\n")) || !bytes.Contains(b, []byte("\n  \"model\": \"opus\"")) {
			t.Fatalf("not indented with two spaces:\n%s", b)
		}
	}
}

func TestSymlinkWritesThrough(t *testing.T) {
	dir := t.TempDir()
	target := filepath.Join(dir, "dotfiles", "settings.json")
	if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(target, []byte(fixture), 0o600); err != nil {
		t.Fatal(err)
	}
	link := filepath.Join(dir, "settings.json")
	if err := os.Symlink(filepath.Join("dotfiles", "settings.json"), link); err != nil {
		t.Fatal(err)
	}
	cmd := Command(bin, 8787)
	if err := Install(link, cmd); err != nil {
		t.Fatal(err)
	}
	st, err := os.Lstat(link)
	if err != nil || st.Mode()&os.ModeSymlink == 0 {
		t.Fatalf("symlink replaced: %v %v", st.Mode(), err)
	}
	if !bytes.Contains(read(t, target), []byte("hook --port 8787")) {
		t.Fatalf("target not updated:\n%s", read(t, target))
	}
	entries, _ := os.ReadDir(filepath.Dir(target))
	if len(entries) != 1 {
		t.Fatalf("stray files beside the target: %v", entries)
	}
}
