package sources

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"claude-status-cube/bridge/internal/pricing"
)

// Local reads Claude Code's own transcripts under ~/.claude/projects. Files
// are append-only, so a byte offset per file means only new lines are parsed.
// The offset advances only past the last complete line ('\n'); a half-written
// line is read whole on the next scan. Lines are split on raw bytes and
// decoded once whole, so a cut multi-byte character cannot be mangled, and a
// line may be any size (no bufio.Scanner).
type Local struct {
	Now func() time.Time // for pruning and the missing-timestamp fallback

	root   string
	open   func(string) (*os.File, error)
	files  map[string]fileState
	seen   map[string]int64 // requestId -> event time (ms), for de-dup
	events []Event
}

type fileState struct {
	offset, size int64
	mtime        time.Time
}

const (
	retainMs = 35 * 86_400_000
	chunk    = 1 << 20
)

// NewLocal returns a source for root; "" means ~/.claude/projects.
func NewLocal(root string) *Local {
	if root == "" {
		home, _ := os.UserHomeDir()
		root = filepath.Join(home, ".claude", "projects")
	}
	return &Local{
		Now:   time.Now,
		root:  root,
		open:  os.Open,
		files: map[string]fileState{},
		seen:  map[string]int64{},
	}
}

func (l *Local) Label() string          { return "local" }
func (l *Local) HasRequestCounts() bool { return true }
func (l *Local) Billed() *Billed        { return nil }
func (l *Local) Events() []Event        { return l.events }

func (l *Local) Refresh(ctx context.Context) ([]Event, error) {
	listed := l.transcripts()
	added := 0
	for _, file := range listed {
		if err := ctx.Err(); err != nil {
			return l.events, err
		}
		st, err := os.Stat(file)
		if err != nil {
			continue // deleted between listing and stat
		}
		prev, known := l.files[file]
		if known && prev.size == st.Size() && prev.mtime.Equal(st.ModTime()) {
			continue
		}
		from := prev.offset
		if st.Size() < from {
			from = 0 // truncated or rewritten
		}
		offset, n := l.ingest(file, from, st.Size())
		l.files[file] = fileState{offset: offset, size: st.Size(), mtime: st.ModTime()}
		added += n
	}
	inList := make(map[string]bool, len(listed))
	for _, f := range listed {
		inList[f] = true
	}
	for f := range l.files {
		if !inList[f] {
			delete(l.files, f)
		}
	}
	l.prune(added > 0)
	return l.events, nil
}

func (l *Local) transcripts() []string {
	var out []string
	_ = filepath.WalkDir(l.root, func(p string, d fs.DirEntry, err error) error {
		if err == nil && !d.IsDir() && strings.HasSuffix(p, ".jsonl") {
			out = append(out, p)
		}
		return nil // unreadable entries are skipped
	})
	return out
}

// ingest parses the complete lines in [start, end) and returns the offset just
// past the last '\n' and the number of events added.
func (l *Local) ingest(file string, start, end int64) (int64, int) {
	f, err := l.open(file)
	if err != nil {
		return start, 0
	}
	defer f.Close()
	pos, done := start, start
	var carry []byte
	count := 0
	buf := make([]byte, chunk)
	for pos < end {
		n := int64(len(buf))
		if end-pos < n {
			n = end - pos
		}
		r, err := f.ReadAt(buf[:n], pos)
		if r == 0 && err != nil {
			break
		}
		pos += int64(r)
		data := append(carry, buf[:r]...)
		lineStart := 0
		for {
			nl := bytes.IndexByte(data[lineStart:], '\n')
			if nl < 0 {
				break
			}
			if l.line(data[lineStart : lineStart+nl]) {
				count++
			}
			lineStart += nl + 1
		}
		carry = append([]byte(nil), data[lineStart:]...)
		done = pos - int64(len(carry))
		if err == io.EOF {
			break
		}
	}
	return done, count
}

type row struct {
	Timestamp string  `json:"timestamp"`
	RequestID *string `json:"requestId"`
	UUID      *string `json:"uuid"`
	CWD       string  `json:"cwd"`
	SessionID string  `json:"sessionId"`
	Model     *string `json:"model"`
	Message   *struct {
		ID    *string        `json:"id"`
		Model *string        `json:"model"`
		Usage *pricing.Usage `json:"usage"`
	} `json:"message"`
}

// line handles one transcript line; true when it added an event.
func (l *Local) line(b []byte) bool {
	if len(b) == 0 || b[0] != '{' {
		return false
	}
	var r row
	if json.Unmarshal(b, &r) != nil || r.Message == nil || r.Message.Usage == nil {
		return false // not usable; complete lines are never retried
	}
	usage := *r.Message.Usage

	t := l.Now().UnixMilli()
	if ts, err := time.Parse(time.RFC3339Nano, r.Timestamp); err == nil && ts.UnixMilli() != 0 {
		t = ts.UnixMilli()
	}
	// requestId ?? message.id ?? uuid
	var id string
	switch {
	case r.RequestID != nil:
		id = *r.RequestID
	case r.Message.ID != nil:
		id = *r.Message.ID
	case r.UUID != nil:
		id = *r.UUID
	}
	if id != "" {
		if _, dup := l.seen[id]; dup {
			return false
		}
		l.seen[id] = t
	}

	model := ""
	if r.Message.Model != nil {
		model = *r.Message.Model
	} else if r.Model != nil {
		model = *r.Model
	}
	cacheWrite := usage.CacheCreationInputTokens
	if usage.CacheCreation != nil {
		cacheWrite = usage.CacheCreation.Ephemeral5m + usage.CacheCreation.Ephemeral1h
	}
	project := "unknown"
	if r.CWD != "" {
		project = filepath.Base(r.CWD)
	}
	var web int64
	if usage.ServerToolUse != nil {
		web = usage.ServerToolUse.WebSearchRequests
	}
	l.events = append(l.events, Event{
		T: t, Model: model, Project: project, Session: r.SessionID,
		Input: usage.InputTokens, CacheRead: usage.CacheReadInputTokens,
		CacheWrite: cacheWrite, Output: usage.OutputTokens, WebSearch: web,
		Cost: pricing.CostUSD(model, usage),
	})
	return true
}

func (l *Local) prune(added bool) {
	cutoff := l.Now().UnixMilli() - retainMs
	kept := l.events[:0:0]
	for _, e := range l.events {
		if e.T >= cutoff {
			kept = append(kept, e)
		}
	}
	l.events = kept
	// An id older than the window can only return as a replay of a row that
	// would be dropped anyway, so the de-dup set is cut at the same point.
	for id, t := range l.seen {
		if t < cutoff {
			delete(l.seen, id)
		}
	}
	if added {
		sort.SliceStable(l.events, func(i, j int) bool { return l.events[i].T < l.events[j].T })
	}
}
