// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/store/store.go (SetStatus, NextPromptID) and backend/internal/daemon/live.go (liveAgentID).

package agents

import (
	"crypto/sha256"
	"encoding/hex"
	"strconv"
	"time"
)

// entry is an Agent plus the tracker's bookkeeping.
type entry struct {
	Agent
	Transcript    string
	TTY           string
	Offset        int64
	Pending       string
	SkipLine      bool // tail: drop input up to the next newline (an over-long line)
	PromptN       int
	Menu          bool
	CustomTitle   string
	AITitle       string
	Recent        []string  // the last recentMax prompts typed, oldest first (transcript)
	AutoTitle     string    // made from Recent by TrackerDeps.Title ("" = none yet)
	AutoFor       string    // the Recent (joined) AutoTitle was made from
	AutoAt        time.Time // the last attempt at an AutoTitle
	Background    bool
	CompactManual bool
	HookSeen      bool
	EndedAt       time.Time

	// Tracker bookkeeping (bridge only).
	LastHook  time.Time // when the last hook arrived
	Missed    int       // successful discoveries in a row without this session
	Live      *Live     // the session as discovery last saw it (nil: never)
	Answering string    // id of the prompt an Answer is typing into
	promptSeq *int      // the tracker's prompt counter (nil: PromptN)
	promptPfx string    // the tracker's prompt id prefix ("": "p")
}

// NextPromptID returns a fresh id for a new blocking prompt: from the
// tracker's counter and prefix when the entry belongs to a tracker, so ids
// never repeat across agents, a re-created agent or a bridge restart; else
// from the entry's own count.
func (e *entry) NextPromptID() string {
	n, pfx := &e.PromptN, "p"
	if e.promptSeq != nil {
		n, pfx = e.promptSeq, e.promptPfx
	}
	*n++
	return pfx + strconv.Itoa(*n)
}

// promptIDPrefix is a tracker's prompt id prefix: "p", its start time in
// base-36 seconds, "-". Ids stay within the card's 15 characters until the
// 10-millionth prompt (and the year 2106).
func promptIDPrefix(start time.Time) string {
	return "p" + strconv.FormatInt(max(start.Unix(), 0), 36) + "-"
}

// SetStatus changes status, stamping StatusSince only on a real change.
func (e *entry) SetStatus(s Status, now time.Time) {
	if e.Status != s {
		e.Status = s
		e.StatusSince = now
	}
	if s != Waiting {
		e.Waiting = nil
	}
	if s != Error {
		e.ErrorText = ""
	}
	if s != Running {
		e.Activity = nil
	}
}

// agentID derives a stable id ([a-z0-9]{6}) from a session id.
func agentID(sessionID string) string {
	sum := sha256.Sum256([]byte("claude-live:" + sessionID))
	return "l" + hex.EncodeToString(sum[:])[:5]
}
