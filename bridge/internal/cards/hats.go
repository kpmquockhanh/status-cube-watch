// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/hats/hats.go (Auto, Clean).

package cards

import (
	"hash/fnv"
	"path/filepath"
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
