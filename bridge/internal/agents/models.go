// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/pricing/pricing.go (labels and windows only).

package agents

import "strings"

type modelInfo struct {
	label  string
	window int64
	match  string
}

// Most specific first.
var models = []modelInfo{
	{"opus 5.5", 1_000_000, "opus-5-5"},
	{"opus 5", 1_000_000, "opus-5"},
	{"opus 4.8", 1_000_000, "opus-4-8"},
	{"opus 4.7", 1_000_000, "opus-4-7"},
	{"opus 4.6", 1_000_000, "opus-4-6"},
	{"fable 5.1", 1_000_000, "fable-5-1"},
	{"fable 5", 1_000_000, "fable-5"},
	{"sonnet 5", 1_000_000, "sonnet-5"},
	{"sonnet 4.6", 1_000_000, "sonnet-4-6"},
	{"haiku 4.5", 200_000, "haiku-4-5"},
}

func lookupModel(modelID string) (modelInfo, bool) {
	for _, m := range models {
		if strings.Contains(modelID, m.match) {
			return m, true
		}
	}
	return modelInfo{}, false
}

// Label is a short display name ("opus 5.5"); falls back to the id without "claude-".
func Label(modelID string) string {
	if m, ok := lookupModel(modelID); ok {
		return m.label
	}
	return strings.TrimPrefix(modelID, "claude-")
}

// Window is the model's context window, 200k when unknown.
func Window(modelID string) int64 {
	if m, ok := lookupModel(modelID); ok {
		return m.window
	}
	return 200_000
}
