// Package limits reads the two rate-limit windows from the usage endpoint.
package limits

import (
	"bytes"
	"encoding/json"
)

// Window is one rate-limit window as the usage endpoint reports it.
// Utilization nil = no reading.
type Window struct {
	Utilization *float64 `json:"utilization"`
	ResetsAt    string   `json:"resets_at"`
}

// UnmarshalJSON is lenient: a utilization that is not a JSON number reads as
// no reading, and a resets_at that is not a string as unknown, instead of
// failing the whole document (cards.mjs only checks `typeof utilization`).
func (w *Window) UnmarshalJSON(b []byte) error {
	// Node treats a window that is not an object as no reading; null never
	// reaches here for a *Window, but a number or string does.
	if t := bytes.TrimSpace(b); len(t) == 0 || t[0] != '{' {
		*w = Window{}
		return nil
	}
	var raw struct {
		Utilization json.RawMessage `json:"utilization"`
		ResetsAt    json.RawMessage `json:"resets_at"`
	}
	if err := json.Unmarshal(b, &raw); err != nil {
		return err
	}
	*w = Window{}
	var u float64
	// null decodes into a float64 without error, so it is ruled out first.
	if len(raw.Utilization) > 0 && !bytes.Equal(raw.Utilization, []byte("null")) && json.Unmarshal(raw.Utilization, &u) == nil {
		w.Utilization = &u
	}
	var s string
	if json.Unmarshal(raw.ResetsAt, &s) == nil {
		w.ResetsAt = s
	}
	return nil
}

// Limits is the latest reading of both windows. Error "" = none.
type Limits struct {
	FiveHour, SevenDay *Window
	Error              string
}
