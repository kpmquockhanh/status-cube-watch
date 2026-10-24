package cards

import (
	"strings"
	"testing"
)

// JS Number.prototype.toFixed rounds the exact binary value, ties up.
func TestToFixedMatchesJS(t *testing.T) {
	for _, c := range []struct {
		x    float64
		d    int
		want string
	}{
		{2.5, 0, "3"},
		{1.25, 1, "1.3"},
		{0.125, 2, "0.13"},
		{1.005, 2, "1.00"},
		{1234.5678, 2, "1234.57"},
		{0, 2, "0.00"},
		{0.5, 0, "1"},
		{0.05, 1, "0.1"},
		{999.5, 0, "1000"},
	} {
		if got := toFixed(c.x, c.d); got != c.want {
			t.Errorf("toFixed(%v, %d) = %q, want %q", c.x, c.d, got, c.want)
		}
	}
}

func TestFit(t *testing.T) {
	for _, c := range []struct {
		in    string
		bytes int
		want  string
	}{
		{"Café", 24, "Cafe"},
		{"a\tb\nc", 24, "a b c"},
		{"đ·–…×", 24, "d--...x"},
		{"ab  ", 4, "ab"},
		{"日本", 24, ""},
		{"Straße Æsir øł “q”", 24, "Strasse AEsir ol \"q\""},
		{"ﬁ①", 24, "fi1"},
		{"x\ufeffy\u0085z", 24, "x yz"},
	} {
		if got := Fit(c.in, c.bytes); got != c.want {
			t.Errorf("Fit(%q, %d) = %q, want %q", c.in, c.bytes, got, c.want)
		}
	}
	if got := Fit(strings.Repeat("x", 40), 24); len(got) != 23 {
		t.Errorf("Fit(40 x, 24) has length %d, want 23", len(got))
	}
}

func TestPrettyModel(t *testing.T) {
	for in, want := range map[string]string{
		"claude-haiku-4-5-20251001": "Haiku 4.5",
		"claude-opus-4-0":           "Opus 4",
		"claude-3-5-haiku":          "Haiku 3.5",
		"":                          "Unknown",
		"gpt-x":                     "Gpt-x",
	} {
		if got := prettyModel(in); got != want {
			t.Errorf("prettyModel(%q) = %q, want %q", in, got, want)
		}
	}
}
