package sources

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"claude-status-cube/bridge/internal/pricing"
)

var baseTime = time.Date(2026, 10, 24, 12, 0, 0, 0, time.UTC)

func line(extra string) string {
	return `{"timestamp":"2026-10-24T11:00:00Z",` + extra + `}`
}

func usageLine(reqID, cwd string) string {
	return line(fmt.Sprintf(`"requestId":%q,"cwd":%q,"sessionId":"s","message":{"model":"claude-sonnet-4-5","usage":{"input_tokens":100,"output_tokens":50,"cache_read_input_tokens":10,"cache_creation_input_tokens":7}}`, reqID, cwd)) + "\n"
}

func write(t *testing.T, path, content string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}
}

func appendTo(t *testing.T, path, content string) {
	t.Helper()
	f, err := os.OpenFile(path, os.O_APPEND|os.O_WRONLY, 0o644)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	if _, err := f.WriteString(content); err != nil {
		t.Fatal(err)
	}
}

func newLocal(root string) *Local {
	l := NewLocal(root)
	l.Now = func() time.Time { return baseTime }
	return l
}

func refresh(t *testing.T, l *Local) []Event {
	t.Helper()
	ev, err := l.Refresh(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	return ev
}

func TestLocalSourceInterface(t *testing.T) {
	var s Source = newLocal(t.TempDir())
	if s.Label() != "local" || !s.HasRequestCounts() || s.Billed() != nil {
		t.Fatal("wrong identity")
	}
}

func TestReadsRecursively(t *testing.T) {
	root := t.TempDir()
	write(t, filepath.Join(root, "p", "a.jsonl"), usageLine("r1", "/x/proj"))
	write(t, filepath.Join(root, "p", "sess", "subagents", "b.jsonl"), usageLine("r2", "/x/proj"))
	ev := refresh(t, newLocal(root))
	if len(ev) != 2 {
		t.Fatalf("events = %d, want 2", len(ev))
	}
	want := pricing.CostUSD("claude-sonnet-4-5", pricing.Usage{InputTokens: 100, OutputTokens: 50, CacheReadInputTokens: 10, CacheCreationInputTokens: 7})
	for _, e := range ev {
		if e.Project != "proj" || e.Cost != want || e.Cost == 0 {
			t.Fatalf("event %+v, want project proj cost %v", e, want)
		}
		if e.Session != "s" || e.Input != 100 || e.Output != 50 || e.CacheRead != 10 || e.CacheWrite != 7 {
			t.Fatalf("event %+v", e)
		}
	}
}

func TestDefaults(t *testing.T) {
	root := t.TempDir()
	write(t, filepath.Join(root, "a.jsonl"), `{"message":{"usage":{"input_tokens":1}}}`+"\n")
	ev := refresh(t, newLocal(root))
	if len(ev) != 1 {
		t.Fatalf("events = %d", len(ev))
	}
	e := ev[0]
	if e.Model != "" || e.Project != "unknown" || e.Session != "" || e.T != baseTime.UnixMilli() {
		t.Fatalf("event %+v", e)
	}
}

func TestDedup(t *testing.T) {
	root := t.TempDir()
	write(t, filepath.Join(root, "a.jsonl"), usageLine("r1", "/x/p")+
		line(`"message":{"id":"m1","usage":{"input_tokens":1}}`)+"\n"+
		line(`"uuid":"u1","message":{"usage":{"input_tokens":1}}`)+"\n")
	write(t, filepath.Join(root, "b.jsonl"), usageLine("r1", "/x/p")+
		line(`"message":{"id":"m1","usage":{"input_tokens":1}}`)+"\n"+
		line(`"uuid":"u1","message":{"usage":{"input_tokens":1}}`)+"\n")
	if n := len(refresh(t, newLocal(root))); n != 3 {
		t.Fatalf("events = %d, want 3", n)
	}
}

func TestIncremental(t *testing.T) {
	root := t.TempDir()
	f := filepath.Join(root, "a.jsonl")
	first := usageLine("r1", "/x/p")
	second := usageLine("r2", "/x/p")
	write(t, f, first)
	l := newLocal(root)
	if n := len(refresh(t, l)); n != 1 {
		t.Fatalf("events = %d", n)
	}
	if got := l.files[f].offset; got != int64(len(first)) {
		t.Fatalf("offset = %d, want %d", got, len(first))
	}
	appendTo(t, f, second)
	if n := len(refresh(t, l)); n != 2 {
		t.Fatalf("events = %d, want 2", n)
	}
	if got := l.files[f].offset; got != int64(len(first)+len(second)) {
		t.Fatalf("offset = %d", got)
	}
}

func TestPartialLine(t *testing.T) {
	root := t.TempDir()
	f := filepath.Join(root, "a.jsonl")
	full := usageLine("r1", "/x/projé")
	cut := strings.Index(full, "é") + 1 // inside the two-byte character
	write(t, f, full[:cut])
	l := newLocal(root)
	if n := len(refresh(t, l)); n != 0 {
		t.Fatalf("events = %d, want 0", n)
	}
	if l.files[f].offset != 0 {
		t.Fatalf("offset = %d, want 0", l.files[f].offset)
	}
	appendTo(t, f, full[cut:])
	ev := refresh(t, l)
	if len(ev) != 1 || ev[0].Project != "projé" {
		t.Fatalf("events = %+v", ev)
	}
}

func TestHugeLine(t *testing.T) {
	root := t.TempDir()
	f := filepath.Join(root, "a.jsonl")
	huge := line(`"message":{"content":"`+strings.Repeat("x", 2<<20)+`"}`) + "\n"
	write(t, f, huge+usageLine("r1", "/x/p"))
	l := newLocal(root)
	if n := len(refresh(t, l)); n != 1 {
		t.Fatalf("events = %d, want 1", n)
	}
	appendTo(t, f, usageLine("r2", "/x/p"))
	if n := len(refresh(t, l)); n != 2 {
		t.Fatalf("events = %d, want 2", n)
	}
}

func TestTruncated(t *testing.T) {
	root := t.TempDir()
	f := filepath.Join(root, "a.jsonl")
	write(t, f, usageLine("r1", "/x/p")+usageLine("r2", "/x/p"))
	l := newLocal(root)
	refresh(t, l)
	write(t, f, usageLine("r3", "/x/p"))
	ev := refresh(t, l)
	if len(ev) != 3 {
		t.Fatalf("events = %d, want 3 (r3 re-read from 0)", len(ev))
	}
}

func TestUnchangedSkipped(t *testing.T) {
	root := t.TempDir()
	write(t, filepath.Join(root, "a.jsonl"), usageLine("r1", "/x/p"))
	l := newLocal(root)
	opens := 0
	l.open = func(p string) (*os.File, error) { opens++; return os.Open(p) }
	refresh(t, l)
	refresh(t, l)
	if opens != 1 {
		t.Fatalf("opens = %d, want 1", opens)
	}
}

func TestPrune(t *testing.T) {
	root := t.TempDir()
	f := filepath.Join(root, "a.jsonl")
	old := `{"timestamp":"2026-09-18T12:00:00Z","requestId":"r1","message":{"usage":{"input_tokens":1}}}` + "\n"
	write(t, f, old) // 36 days before baseTime
	l := newLocal(root)
	if n := len(refresh(t, l)); n != 0 {
		t.Fatalf("events = %d, want 0 (old event pruned)", n)
	}
	appendTo(t, f, usageLine("r1", "/x/p"))
	if n := len(refresh(t, l)); n != 1 {
		t.Fatalf("events = %d, want 1 (id countable again)", n)
	}
}

func TestDeletedFile(t *testing.T) {
	root := t.TempDir()
	f := filepath.Join(root, "a.jsonl")
	write(t, f, usageLine("r1", "/x/p"))
	l := newLocal(root)
	refresh(t, l)
	os.Remove(f)
	refresh(t, l)
	if len(l.files) != 0 {
		t.Fatalf("files = %v", l.files)
	}
}

func TestCacheWriteSplit(t *testing.T) {
	root := t.TempDir()
	write(t, filepath.Join(root, "a.jsonl"),
		line(`"requestId":"a","message":{"usage":{"cache_creation_input_tokens":99,"cache_creation":{"ephemeral_5m_input_tokens":3,"ephemeral_1h_input_tokens":4}}}`)+"\n"+
			line(`"requestId":"b","message":{"usage":{"cache_creation_input_tokens":99}}`)+"\n")
	ev := refresh(t, newLocal(root))
	if len(ev) != 2 || ev[0].CacheWrite != 7 || ev[1].CacheWrite != 99 {
		t.Fatalf("events = %+v", ev)
	}
}

func TestBadLinesSkipped(t *testing.T) {
	root := t.TempDir()
	write(t, filepath.Join(root, "a.jsonl"), "not json\n{bad\n"+
		line(`"requestId":"f","message":{"usage":{"input_tokens":1.5}}`)+"\n"+usageLine("ok", "/x/p"))
	if n := len(refresh(t, newLocal(root))); n != 1 {
		t.Fatalf("events = %d, want 1", n)
	}
}

func TestMissingRoot(t *testing.T) {
	if n := len(refresh(t, newLocal(filepath.Join(t.TempDir(), "nope")))); n != 0 {
		t.Fatalf("events = %d", n)
	}
}
