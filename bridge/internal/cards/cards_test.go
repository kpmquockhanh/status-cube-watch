package cards

import (
	"encoding/json"
	"fmt"
	"os"
	"reflect"
	"testing"
	"time"
	_ "time/tzdata" // the golden cases need Asia/Ho_Chi_Minh on any machine

	"claude-status-cube/bridge/internal/agents"
	"claude-status-cube/bridge/internal/limits"
	"claude-status-cube/bridge/internal/mail"
	"claude-status-cube/bridge/internal/sources"
)

// goldenCase is one entry of testdata/cases-*.json, written by gen.mjs from
// the Node card builder. `want` is Node's payload: the truth.
type goldenCase struct {
	Name   string `json:"name"`
	Now    int64  `json:"now"`
	Events []struct {
		T          int64   `json:"t"`
		Model      *string `json:"model"`
		Project    *string `json:"project"`
		Session    *string `json:"session"`
		Input      int64   `json:"input"`
		CacheRead  int64   `json:"cacheRead"`
		CacheWrite int64   `json:"cacheWrite"`
		Output     int64   `json:"output"`
		WebSearch  int64   `json:"webSearch"`
		Cost       float64 `json:"cost"`
	} `json:"events"`
	Opts struct {
		Source           string `json:"source"`
		HasRequestCounts bool   `json:"hasRequestCounts"`
		Billed           *struct {
			ByDay map[string]float64 `json:"byDay"`
			Total float64            `json:"total"`
		} `json:"billed"`
		Error  *string `json:"error"`
		Limits *struct {
			FiveHour *limits.Window `json:"five_hour"`
			SevenDay *limits.Window `json:"seven_day"`
			Error    *string        `json:"error"`
		} `json:"limits"`
		Mail *struct {
			Unread *int    `json:"unread"`
			Stale  bool    `json:"stale"`
			Error  *string `json:"error"`
		} `json:"mail"`
		ExtraCards bool `json:"extraCards"`
	} `json:"opts"`
	Want json.RawMessage `json:"want"`
}

func str(p *string) string {
	if p == nil {
		return ""
	}
	return *p
}

func (c goldenCase) inputs() ([]sources.Event, Opts) {
	events := make([]sources.Event, 0, len(c.Events))
	for _, e := range c.Events {
		events = append(events, sources.Event{
			T: e.T, Model: str(e.Model), Project: str(e.Project), Session: str(e.Session),
			Input: e.Input, CacheRead: e.CacheRead, CacheWrite: e.CacheWrite, Output: e.Output,
			WebSearch: e.WebSearch, Cost: e.Cost,
		})
	}
	o := Opts{
		Source:           c.Opts.Source,
		HasRequestCounts: c.Opts.HasRequestCounts,
		Error:            str(c.Opts.Error),
		ExtraCards:       c.Opts.ExtraCards,
	}
	if b := c.Opts.Billed; b != nil {
		o.Billed = &sources.Billed{ByDay: b.ByDay, Total: b.Total}
	}
	if l := c.Opts.Limits; l != nil {
		o.Limits = &limits.Limits{FiveHour: l.FiveHour, SevenDay: l.SevenDay, Error: str(l.Error)}
	}
	if m := c.Opts.Mail; m != nil {
		o.Mail = &mail.Mail{Unread: m.Unread, Stale: m.Stale, Error: str(m.Error)}
	}
	return events, o
}

func TestGolden(t *testing.T) {
	hcm, err := time.LoadLocation("Asia/Ho_Chi_Minh")
	if err != nil {
		t.Fatal(err)
	}
	for file, loc := range map[string]*time.Location{
		"testdata/cases-utc.json": time.UTC,
		"testdata/cases-hcm.json": hcm,
	} {
		raw, err := os.ReadFile(file)
		if err != nil {
			t.Fatal(err)
		}
		var cases []goldenCase
		if err := json.Unmarshal(raw, &cases); err != nil {
			t.Fatalf("%s: %v", file, err)
		}
		if len(cases) == 0 {
			t.Fatalf("%s: no cases", file)
		}
		for _, c := range cases {
			events, opts := c.inputs()
			got, err := json.Marshal(Build(events, opts, time.UnixMilli(c.Now).In(loc)))
			if err != nil {
				t.Fatal(err)
			}
			var gotTree, wantTree any
			if err := json.Unmarshal(got, &gotTree); err != nil {
				t.Fatal(err)
			}
			if err := json.Unmarshal(c.Want, &wantTree); err != nil {
				t.Fatal(err)
			}
			if !reflect.DeepEqual(gotTree, wantTree) {
				t.Errorf("%s / %s:\n got  %s\n want %s", file, c.Name, got, compact(c.Want))
			}
		}
	}
}

// The firmware keeps at most MAX_CARDS = 8 cards and drops the rest silently,
// so the longest deck the bridge can build must fit, Agents card included.
func TestBuildCardCountFitsFirmware(t *testing.T) {
	const firmwareMaxCards = 8
	var list []agents.Agent
	for i := range 8 {
		list = append(list, agent(fmt.Sprintf("a%05d", i), fmt.Sprintf("proj%d", i), agents.Running, time.Minute))
	}
	o := Opts{Error: "boom", ExtraCards: true, Agents: list, ShowAgents: true, Full: true}
	p := Build(budgetEvents(), o, agentsNow)
	if len(p.Cards) > firmwareMaxCards {
		t.Fatalf("%d cards, the firmware keeps %d", len(p.Cards), firmwareMaxCards)
	}
	if agentsCard(t, p) == nil {
		t.Errorf("no Agents card in %d cards", len(p.Cards))
	}
}

func compact(b []byte) string {
	var v any
	_ = json.Unmarshal(b, &v)
	out, _ := json.Marshal(v)
	return string(out)
}

func TestWindowLenientDecode(t *testing.T) {
	var w struct {
		A, B, C, D limits.Window
	}
	err := json.Unmarshal([]byte(`{"A":{"utilization":"12","resets_at":"x"},"B":{"utilization":null,"resets_at":null},
		"C":{"utilization":12.5,"resets_at":5},"D":{}}`), &w)
	if err != nil {
		t.Fatal(err)
	}
	if w.A.Utilization != nil || w.A.ResetsAt != "x" {
		t.Errorf("A = %+v", w.A)
	}
	if w.B.Utilization != nil || w.B.ResetsAt != "" {
		t.Errorf("B = %+v", w.B)
	}
	if w.C.Utilization == nil || *w.C.Utilization != 12.5 || w.C.ResetsAt != "" {
		t.Errorf("C = %+v", w.C)
	}
	if w.D.Utilization != nil || w.D.ResetsAt != "" {
		t.Errorf("D = %+v", w.D)
	}
}
