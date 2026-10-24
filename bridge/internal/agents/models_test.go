package agents

import "testing"

func TestLabelWindow(t *testing.T) {
	cases := []struct {
		id    string
		label string
		win   int64
	}{
		{"claude-opus-5-5", "opus 5.5", 1_000_000},
		{"claude-haiku-4-5-20251001", "haiku 4.5", 200_000},
		{"claude-x-9", "x-9", 200_000},
		{"", "", 200_000},
	}
	for _, c := range cases {
		if got := Label(c.id); got != c.label {
			t.Errorf("Label(%q)=%q want %q", c.id, got, c.label)
		}
		if got := Window(c.id); got != c.win {
			t.Errorf("Window(%q)=%d want %d", c.id, got, c.win)
		}
	}
}
