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
- `backend/internal/hats/hats.go` (`Auto`, `Clean`, the shape and colour order; ported as `cards.HatOf`)
- `firmware/devices/ws169-panel/src/ui.c` (`meta`, `verb`, `ago`, `pose_of`: tile status lines and the celebrate window; reworded in `cards/agents.go`)
- `backend/internal/claude/live.go` (`ActiveSessions`, `ttysOf`, `LiveSessions`, `isInteractiveClaude`, `sessionArg`, `newestTranscript`, `processCwds`, `findTranscript`, `projectName`)
- `backend/internal/claude/transcript.go`
- `backend/internal/claude/sessions.go` (`Subagents`, `TranscriptTurn`, `subagentTail`, `IsSessionID`)
- `backend/internal/claude/pane.go`
- `backend/internal/claude/birth_darwin.go`, `birth_other.go`
- `backend/internal/claude/hooks.go` (`Event`, `Apply`, prompts, `ToolDetail`, `ShortPath`, `field`, `DefaultPermissionOptions`, text helpers)
- `backend/internal/claude/claude_test.go`, `sessions_test.go`, `live_test.go`; `backend/internal/daemon/live_test.go`

The web board (`bridge/board/`) adapts agents-deck's web dashboard at the same commit:

- `backend/internal/web/static/app.js`: the "sprite" section (critter, poses, hat art) as `critter.js`, unchanged but for its exports; the tiles, detail panel, hat picker and prompt in `board.js`
- `backend/internal/web/static/app.css`: as `board.css`, trimmed
- `backend/internal/web/static/index.html`: the page's skeleton

The firmware's critter and hat art (`firmware/src/critter.cpp`, `hat_art.cpp`) is a port of the same sprite code, and the Agents card's tiles and detail screen follow its board and panel views.

agents-deck is the work of its author, [brushknight](https://github.com/brushknight); all credit for these parts goes to that project. agents-deck publishes no licence, so this notice grants none for the code adapted from it.
