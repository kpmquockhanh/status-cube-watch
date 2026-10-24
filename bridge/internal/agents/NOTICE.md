# Notice

Files in this package are adapted from agents-deck, https://github.com/brushknight/agents-deck, commit `699bba8`.

Source files used by this plan:

- `backend/internal/model/model.go`
- `backend/internal/store/store.go`
- `backend/internal/pricing/pricing.go`
- `backend/internal/daemon/live.go` (`liveAgentID`, `syncLive`, `liveStatus`, `discover`)
- `backend/internal/daemon/daemon.go` (`Hook`, `Answer`, `subagents`, `scanMenu`, `LookPathLogin`, `shellJoin`)
- `backend/internal/daemon/accounts.go` (`activeSessions`, single account)
- `backend/internal/tmux/tmux.go` (`run`, `SendKeys`, `Capture`, `PaneTTYs`; default server instead of `-L agentsterm`)
- `backend/internal/claude/live.go` (`ActiveSessions`, `ttysOf`, `LiveSessions`, `isInteractiveClaude`, `sessionArg`, `newestTranscript`, `processCwds`, `findTranscript`, `projectName`)
- `backend/internal/claude/transcript.go`
- `backend/internal/claude/sessions.go` (`Subagents`, `TranscriptTurn`, `subagentTail`, `IsSessionID`)
- `backend/internal/claude/pane.go`
- `backend/internal/claude/birth_darwin.go`, `birth_other.go`
- `backend/internal/claude/hooks.go` (`Event`, `Apply`, prompts, `ToolDetail`, `ShortPath`, `field`, `DefaultPermissionOptions`, text helpers)
- `backend/internal/claude/claude_test.go`, `sessions_test.go`, `live_test.go`; `backend/internal/daemon/live_test.go`

No licence was published; copied with the owner of this private repository's consent. Do not make this repository public without the agents-deck author's permission.
