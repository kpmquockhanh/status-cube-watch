// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/claude/hooks.go (text helpers).

package agents

import "strings"

func oneLine(s string) string { return strings.Join(strings.Fields(s), " ") }

func clip(s string, n int) string {
	r := []rune(s)
	if len(r) <= n {
		return s
	}
	return string(r[:n-1]) + "…"
}

// lower lowercases the first letter unless it starts an acronym ("API").
func lower(s string) string {
	s = strings.TrimSpace(s)
	if s == "" {
		return s
	}
	r := []rune(s)
	if len(r) > 1 && r[1] >= 'a' && r[1] <= 'z' {
		r[0] = []rune(strings.ToLower(string(r[0])))[0]
	}
	return string(r)
}
