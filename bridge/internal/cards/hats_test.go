package cards

import "testing"

// Expected values come from agents-deck's hats.Auto (its Auto/Clean text
// copied into a throwaway module and run on these folders).
func TestHatOfMatchesAgentsDeck(t *testing.T) {
	cases := []struct {
		folder       string
		shape, color int
	}{
		{"/Users/me/code/checkout-api", 1, 5},             // cap, pink
		{"/Users/me/code/docs-site", 1, 1},                // cap, blue
		{"/Users/me/code/claude-status-cube", 1, 2},       // cap, yellow
		{"/work/infra-tf/", 4, 1},                         // wizard, blue (trailing slash cleaned)
		{"/a", 2, 0},                                      // beanie, teal
		{"/Users/me/code/mobile-app/../mobile-app", 6, 0}, // chef, teal (cleaned)
		{"/tmp/scratch", 3, 1},                            // helmet, blue
		{"/", 10, 7},                                      // top hat, ink
	}
	for _, tc := range cases {
		if s, c := HatOf(tc.folder); s != tc.shape || c != tc.color {
			t.Errorf("HatOf(%q) = %d,%d, want %d,%d", tc.folder, s, c, tc.shape, tc.color)
		}
	}
}
