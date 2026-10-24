package pricing

import (
	"math"
	"testing"
)

func TestRatesLongestPrefix(t *testing.T) {
	cases := map[string]float64{
		"claude-opus-5-5":           4,
		"claude-opus-5":             5,
		"claude-haiku-4-5-20251001": 1,
		"claude-opus-4-0":           15,
		"claude-opus-5-55":          5, // opus-5-5 lacks a - boundary, opus-5 has one (same as Node)
		"claude-opus-50":            2, // no - boundary: fallback
		"":                          2,
	}
	for m, want := range cases {
		if got := Rates(m).In; got != want {
			t.Errorf("Rates(%q).In = %v, want %v", m, got, want)
		}
	}
}

func near(t *testing.T, name string, got, want float64) {
	t.Helper()
	if math.Abs(got-want) > 1e-9 {
		t.Errorf("%s = %v, want %v", name, got, want)
	}
}

func TestCost(t *testing.T) {
	near(t, "input", CostUSD("claude-sonnet-4-5", Usage{InputTokens: 1e6}), 3.0)
	near(t, "read sonnet", CostUSD("claude-sonnet-4-5", Usage{CacheReadInputTokens: 1e6}), 0.3)
	near(t, "read opus-5-5", CostUSD("claude-opus-5-5", Usage{CacheReadInputTokens: 1e6}), 0.2)
	near(t, "5m", CostUSD("claude-sonnet-4-5", Usage{CacheCreation: &CacheCreation{Ephemeral5m: 1e6}}), 3.75)
	near(t, "1h", CostUSD("claude-sonnet-4-5", Usage{CacheCreation: &CacheCreation{Ephemeral1h: 1e6}}), 6.0)
	near(t, "no split", CostUSD("claude-sonnet-4-5", Usage{CacheCreationInputTokens: 1e6}), 3.75)
	near(t, "fast", CostUSD("claude-opus-5-5", Usage{Speed: "fast", OutputTokens: 1e6}), 40)
	near(t, "fast none", CostUSD("claude-opus-4-6", Usage{Speed: "fast", OutputTokens: 1e6}), 25)
	near(t, "us", CostUSD("claude-sonnet-4-5", Usage{InputTokens: 1e6, InferenceGeo: "us"}), 3.3)
}
