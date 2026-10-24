// Package sources holds what a usage source (local transcripts or the Admin
// API) hands the card builder.
package sources

import "context"

// Event is one billed request.
type Event struct {
	T                                               int64  // epoch ms
	Model, Project, Session                         string // "" = null in Node
	Input, CacheRead, CacheWrite, Output, WebSearch int64
	Cost                                            float64
}

// Billed is the Admin API cost report: billing truth, preferred over estimates.
type Billed struct {
	ByDay map[string]float64 // key YYYY-MM-DD
	Total float64
}

// Source is a usage source.
type Source interface {
	Label() string
	Refresh(ctx context.Context) ([]Event, error)
	Events() []Event // last good events, after a failed Refresh too
	HasRequestCounts() bool
	Billed() *Billed // nil unless admin
}
