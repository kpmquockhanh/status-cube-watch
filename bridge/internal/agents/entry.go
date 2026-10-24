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
	PromptN       int
	Menu          bool
	CustomTitle   string
	AITitle       string
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
}

// NextPromptID returns a fresh id for a new blocking prompt: from the
// tracker's counter when the entry belongs to a tracker, so ids never repeat
// across agents or a re-created agent; else from the entry's own count.
func (e *entry) NextPromptID() string {
	n := &e.PromptN
	if e.promptSeq != nil {
		n = e.promptSeq
	}
	*n++
	return "p" + strconv.Itoa(*n)
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
