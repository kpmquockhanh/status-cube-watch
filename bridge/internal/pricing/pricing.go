// Package pricing ports bridge/pricing.mjs: USD per million tokens, from
// platform.claude.com/docs/en/about-claude/pricing.
//
// Cache pricing is a multiplier on the input rate: 5-minute write 1.25x,
// 1-hour write 2.0x, read Read x (0.10x unless the model overrides it).
// Fast mode (usage.speed "fast") uses Rate.Fast where present; models without
// one bill at standard rates. US-only inference (inference_geo "us") is 1.1x
// on every category.
package pricing

import (
	"sort"
	"strings"
)

type CacheCreation struct {
	Ephemeral5m int64 `json:"ephemeral_5m_input_tokens"`
	Ephemeral1h int64 `json:"ephemeral_1h_input_tokens"`
}

// Usage is a transcript line's message.usage, decoded straight from JSON.
type Usage struct {
	InputTokens              int64          `json:"input_tokens"`
	CacheReadInputTokens     int64          `json:"cache_read_input_tokens"`
	CacheCreationInputTokens int64          `json:"cache_creation_input_tokens"`
	OutputTokens             int64          `json:"output_tokens"`
	CacheCreation            *CacheCreation `json:"cache_creation"` // nil = no split recorded
	Speed                    string         `json:"speed"`
	InferenceGeo             string         `json:"inference_geo"`
	ServerToolUse            *struct {
		WebSearchRequests int64 `json:"web_search_requests"`
	} `json:"server_tool_use"`
}

type Rate struct {
	In, Out, Read float64 // Read 0 = default 0.1
	Fast          *struct{ In, Out float64 }
}

func fast(in, out float64) *struct{ In, Out float64 } {
	return &struct{ In, Out float64 }{in, out}
}

var models = map[string]Rate{
	"claude-fable-5-1":  {In: 10, Out: 50, Read: 0.025},
	"claude-mythos-5-1": {In: 10, Out: 50, Read: 0.025},
	"claude-fable-5":    {In: 10, Out: 50},
	"claude-mythos-5":   {In: 10, Out: 50},
	"claude-opus-5-5":   {In: 4, Out: 20, Read: 0.05, Fast: fast(8, 40)},
	"claude-opus-5":     {In: 5, Out: 25, Fast: fast(10, 50)},
	"claude-opus-4-8":   {In: 5, Out: 25, Fast: fast(10, 50)},
	"claude-opus-4-7":   {In: 5, Out: 25},
	"claude-opus-4-6":   {In: 5, Out: 25},
	"claude-opus-4-5":   {In: 5, Out: 25},
	"claude-opus-4-1":   {In: 15, Out: 75},
	"claude-opus-4":     {In: 15, Out: 75}, // also claude-opus-4-0, claude-opus-4-20250514
	"claude-sonnet-5-5": {In: 2, Out: 10},
	"claude-sonnet-5":   {In: 2, Out: 10},
	"claude-sonnet-4-6": {In: 3, Out: 15},
	"claude-sonnet-4-5": {In: 3, Out: 15},
	"claude-sonnet-4":   {In: 3, Out: 15}, // also claude-sonnet-4-0, claude-sonnet-4-20250514
	"claude-haiku-4-5":  {In: 1, Out: 5},
	"claude-3-5-haiku":  {In: 0.8, Out: 4},
}

// Unknown models fall back to Sonnet-tier rates.
var fallback = Rate{In: 2, Out: 10}

var byLength = func() []string {
	ids := make([]string, 0, len(models))
	for id := range models {
		ids = append(ids, id)
	}
	sort.Slice(ids, func(i, j int) bool {
		if len(ids[i]) != len(ids[j]) {
			return len(ids[i]) > len(ids[j])
		}
		return ids[i] < ids[j]
	})
	return ids
}()

// Rates returns the longest id matching on a '-' boundary, so claude-opus-5-5
// is not priced as claude-opus-5 and dated ids resolve to their base model.
func Rates(model string) Rate {
	if model == "" {
		return fallback
	}
	if r, ok := models[model]; ok {
		return r
	}
	for _, id := range byLength {
		if strings.HasPrefix(model, id) && len(model) > len(id) && model[len(id)] == '-' {
			return models[id]
		}
	}
	return fallback
}

func CostUSD(model string, u Usage) float64 {
	r := Rates(model)
	in, out := r.In, r.Out
	if u.Speed == "fast" && r.Fast != nil {
		in, out = r.Fast.In, r.Fast.Out
	}
	read := r.Read
	if read == 0 {
		read = 0.1
	}
	c5 := u.CacheCreationInputTokens // no split recorded: assume 5-minute writes
	var c1h int64
	if u.CacheCreation != nil {
		c5 = u.CacheCreation.Ephemeral5m
		c1h = u.CacheCreation.Ephemeral1h
	}
	geo := 1.0
	if u.InferenceGeo == "us" {
		geo = 1.1
	}
	return geo *
		(float64(u.InputTokens)*in +
			float64(u.CacheReadInputTokens)*in*read +
			float64(c5)*in*1.25 +
			float64(c1h)*in*2.0 +
			float64(u.OutputTokens)*out) /
		1_000_000
}
