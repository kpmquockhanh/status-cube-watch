// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/hats/hats.go (Auto, Clean).

package cards

import (
	"cmp"
	"hash/fnv"
	"path/filepath"

	"claude-status-cube/bridge/internal/agents"
)

// agents-deck's 13 shapes (hard hat, cap, beanie, helmet, wizard, beret, chef,
// crown, headphones, propeller, top hat, cowboy, bandana) and 8 colours (teal,
// blue, yellow, green, purple, pink, sky, ink), in hash order. The firmware
// draws the Nth shape in the Nth colour; the names live only in the art.
const (
	hatShapes = 13
	hatColors = 8
)

// HatOf picks the hat for a working folder from one stable FNV-1a 32 hash of
// the cleaned path, exactly as agents-deck's hats.Auto. The caller skips the
// hat for an empty folder.
func HatOf(folder string) (shape, color int) {
	h := fnv.New32a()
	if folder != "" {
		folder = filepath.Clean(folder)
	}
	_, _ = h.Write([]byte(folder))
	v := h.Sum32()
	return int(v % hatShapes), int((v >> 8) % hatColors)
}

// HatKey names an agent's hat: its session, so every session wears its own
// (agents-deck picks one per folder; sessions in one folder would all look
// alike). The automatic hat is HatOf(HatKey(a)), random-looking but stable
// across bridge restarts, and a hat picked on the cube is stored under it.
// A session without an id falls back to its agent id.
func HatKey(a agents.Agent) string {
	return "session:" + cmp.Or(a.SessionID, a.ID)
}
