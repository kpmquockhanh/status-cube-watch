// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/claude/transcript.go and sessions.go (Subagents, TranscriptTurn, subagentTail).

package agents

import (
	"bytes"
	"encoding/json"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// One tail call reads up to maxChunks × maxRead (1 GiB): a whole long
// session in one go, without holding it all in memory at once.
const (
	maxRead   = 8 << 20
	maxChunks = 128
)

// maxPending caps the unfinished line kept between tail calls: past it the
// line is dropped and skipped up to its newline (a var for tests).
var maxPending = 16 << 20

// keepPending keeps the unfinished line part, or drops it when too long.
func keepPending(e *entry, part []byte) {
	if len(part) > maxPending {
		e.Pending, e.SkipLine = "", true
		return
	}
	e.Pending = string(part)
}

// CompactTool is the activity shown while Claude compacts the conversation.
const CompactTool = "Compact"

type line struct {
	Type      string `json:"type"`
	Subtype   string `json:"subtype"`
	GitBranch string `json:"gitBranch"`
	AITitle   string `json:"aiTitle"`
	Custom    string `json:"customTitle"`
	IsAPIErr  bool   `json:"isApiErrorMessage"`
	Message   *struct {
		ID      string          `json:"id"`
		Model   string          `json:"model"`
		Content json.RawMessage `json:"content"`
		Usage   *struct {
			Input      int64 `json:"input_tokens"`
			CacheRead  int64 `json:"cache_read_input_tokens"`
			CacheWrite int64 `json:"cache_creation_input_tokens"`
		} `json:"usage"`
	} `json:"message"`
}

// endCompact clears a running compaction: a /compact you typed leaves the
// agent idle (it was waiting for you before), an auto-compact goes on working.
func endCompact(e *entry, now time.Time) bool {
	if e.Activity == nil || e.Activity.Tool != CompactTool {
		return false
	}
	if e.CompactManual {
		e.SetStatus(Idle, now)
	} else {
		e.Activity = nil
	}
	e.CompactManual = false
	return true
}

// displayTitle is a rename (unless it is just the folder), else Claude's own
// title, else the folder.
func displayTitle(e *entry) string {
	folder := e.Folder
	if folder == "" && e.Cwd != "" {
		folder = filepath.Base(e.Cwd)
	}
	switch {
	case e.CustomTitle != "" && e.CustomTitle != folder:
		return e.CustomTitle
	case e.AITitle != "":
		return e.AITitle
	case folder != "":
		return folder
	}
	return e.Title
}

// tail reads transcript lines appended since the last call and folds context,
// model, branch, Claude's own title and API errors into the agent.
func tail(e *entry, now time.Time) bool {
	if e.Transcript == "" {
		return false
	}
	f, err := os.Open(e.Transcript)
	if err != nil {
		return false
	}
	defer f.Close()
	if st, err := f.Stat(); err != nil || st.Size() < e.Offset {
		e.Offset, e.Pending, e.SkipLine = 0, "", false // truncated or replaced
	} else if st.Size() == e.Offset {
		return false
	}
	if _, err := f.Seek(e.Offset, io.SeekStart); err != nil {
		return false
	}
	// Catch up in one call, chunk by chunk, so the card never shows the
	// numbers of half-read history (a restart re-reads from the start).
	changed := false
	var lastCtx int64
	var lastModel string
	for range maxChunks {
		buf, _ := io.ReadAll(io.LimitReader(f, maxRead))
		if len(buf) == 0 {
			break
		}
		e.Offset += int64(len(buf))
		if e.SkipLine { // the rest of a line too long to keep
			nl := bytes.IndexByte(buf, '\n')
			if nl < 0 {
				continue
			}
			buf, e.SkipLine = buf[nl+1:], false
		}
		data := append([]byte(e.Pending), buf...)
		last := bytes.LastIndexByte(data, '\n')
		if last < 0 {
			keepPending(e, data)
			continue
		}
		keepPending(e, data[last+1:])

		for _, raw := range bytes.Split(data[:last], []byte{'\n'}) {
			var l line
			if len(bytes.TrimSpace(raw)) == 0 || json.Unmarshal(raw, &l) != nil {
				continue
			}
			if l.GitBranch != "" && l.GitBranch != e.Branch {
				e.Branch, changed = l.GitBranch, true
			}
			switch l.Type {
			case "system":
				// The compaction finished (also caught by SessionStart "compact").
				if l.Subtype == "compact_boundary" && endCompact(e, now) {
					changed = true
				}
			case "ai-title":
				if l.AITitle != "" && l.AITitle != e.AITitle {
					e.AITitle, changed = l.AITitle, true
				}
			case "custom-title":
				if l.Custom != e.CustomTitle {
					e.CustomTitle, changed = l.Custom, true
				}
			case "assistant":
				if l.Message == nil {
					continue
				}
				if l.IsAPIErr {
					e.SetStatus(Error, now)
					e.ErrorText = apiErrorText(l.Message.Content)
					changed = true
					continue
				}
				if l.Message.Usage == nil || l.Message.ID == "" || l.Message.Model == "<synthetic>" {
					continue
				}
				u := l.Message.Usage
				lastCtx = u.Input + u.CacheRead + u.CacheWrite
				lastModel = l.Message.Model
				if e.Status == Error {
					e.SetStatus(Running, now) // recovered: a real reply arrived
				}
				changed = true
			}
		}
	}
	if lastModel != "" {
		e.Model = lastModel
		e.ModelLabel = Label(lastModel)
		e.Context.Used, e.Context.Window = lastCtx, Window(lastModel)
		if lastCtx > e.Context.Window { // e.g. a 1M beta on a model we think is 200k
			e.Context.Window = 1_000_000
		}
	}
	if changed {
		e.Title = displayTitle(e) // names follow Claude's titles live
	}
	return changed
}

func apiErrorText(content json.RawMessage) string {
	var blocks []struct {
		Text string `json:"text"`
	}
	_ = json.Unmarshal(content, &blocks)
	for _, b := range blocks {
		if t := strings.TrimSpace(b.Text); t != "" {
			t = strings.TrimPrefix(t, "API Error: ")
			return clip(lower(oneLine(t)), 120)
		}
	}
	return "api error"
}

// subagents lists the subagents of the session at transcript that are still
// working, from their own transcripts (<session>/subagents/agent-*.jsonl):
// not ended with a final reply, and written within window, or within
// toolWindow while a tool call is still waiting for its result (a long build
// or test run writes nothing until it returns). Subagents can keep working
// while the main loop is idle (background agents, or another process on the
// same session) and fire no hook of ours.
func subagents(transcript string, now time.Time, window, toolWindow time.Duration) []Subagent {
	if transcript == "" {
		return nil
	}
	files, _ := filepath.Glob(filepath.Join(strings.TrimSuffix(transcript, ".jsonl"), "subagents", "agent-*.jsonl"))
	type found struct {
		s     Subagent
		since time.Time
	}
	var out []found
	for _, f := range files {
		st, err := os.Stat(f)
		if err != nil || now.Sub(st.ModTime()) > max(window, toolWindow) {
			continue
		}
		done, inTool, tool, start := subagentTail(f, st.Size())
		if done || (!inTool && now.Sub(st.ModTime()) > window) {
			continue
		}
		id := strings.TrimPrefix(strings.TrimSuffix(filepath.Base(f), ".jsonl"), "agent-")
		s := Subagent{ID: id, Tool: tool}
		var meta struct {
			Type        string `json:"agentType"`
			Description string `json:"description"`
		}
		if b, err := os.ReadFile(strings.TrimSuffix(f, ".jsonl") + ".meta.json"); err == nil && json.Unmarshal(b, &meta) == nil {
			s.Title, s.Type = clip(oneLine(meta.Description), 80), meta.Type
		}
		if s.Title == "" {
			s.Title = "subagent"
		}
		out = append(out, found{s, start})
	}
	if len(out) == 0 {
		return nil
	}
	sort.Slice(out, func(i, j int) bool { return out[i].since.Before(out[j].since) })
	subs := make([]Subagent, len(out))
	for i, f := range out {
		subs[i] = f.s
	}
	return subs
}

// transcriptTurn reads the end of a session transcript: whether a turn is in
// progress (the last message isn't Claude's final reply, and the file was
// written within window or a tool call is still out), and the tool of the
// latest call.
func transcriptTurn(path string, now time.Time, window time.Duration) (running bool, tool string) {
	st, err := os.Stat(path)
	if err != nil {
		return false, ""
	}
	done, inTool, tool, _ := subagentTail(path, st.Size())
	if done {
		return false, ""
	}
	return inTool || now.Sub(st.ModTime()) < window, tool
}

// subagentTail reads the end of a subagent transcript: whether its last
// message is a final reply, whether it is a tool call still waiting for its
// result, the tool of its latest call, and roughly when it started.
func subagentTail(path string, size int64) (done, inTool bool, tool string, start time.Time) {
	f, err := os.Open(path)
	if err != nil {
		return true, false, "", start
	}
	defer f.Close()
	if st, err := f.Stat(); err == nil {
		start = birthTime(st)
	}
	off := max(0, size-256<<10)
	buf := make([]byte, size-off)
	n, _ := f.ReadAt(buf, off)
	lines := bytes.Split(bytes.TrimSpace(buf[:n]), []byte("\n"))
	var m struct {
		Type    string `json:"type"`
		Message *struct {
			StopReason string `json:"stop_reason"`
			Content    []struct {
				Type string `json:"type"`
				Name string `json:"name"`
			} `json:"content"`
		} `json:"message"`
	}
	decided := false
	for i := len(lines) - 1; i >= 0 && (tool == "" || !decided); i-- {
		m.Type, m.Message = "", nil
		if json.Unmarshal(lines[i], &m) != nil || m.Message == nil {
			continue
		}
		if !decided && (m.Type == "assistant" || m.Type == "user") {
			decided = true
			done = m.Type == "assistant" && m.Message.StopReason == "end_turn"
			// Tool calls usually carry no stop_reason: look for the block.
			for _, c := range m.Message.Content {
				inTool = inTool || (m.Type == "assistant" && c.Type == "tool_use")
			}
		}
		if m.Type == "assistant" && tool == "" {
			for _, c := range m.Message.Content {
				if c.Type == "tool_use" {
					tool = c.Name
				}
			}
		}
	}
	return done, inTool, tool, start
}
