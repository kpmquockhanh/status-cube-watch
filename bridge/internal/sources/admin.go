package sources

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"sort"
	"strconv"
	"strings"
	"time"

	"claude-status-cube/bridge/internal/pricing"
)

const (
	adminBase        = "https://api.anthropic.com/v1/organizations"
	adminUA          = "claude-status-cube/1.0"
	adminTimeout     = 15 * time.Second // per page; a hung request must not stall the refresh loop
	adminUsageHours  = 168              // API max for 1h buckets
	adminMaxPages    = 20
	adminIsoLayout   = "2006-01-02T15:04:05.000Z"
	adminErrBodyKeep = 300
)

// Admin reads organization-wide usage and billed cost from the Anthropic
// Usage & Cost Admin API: hourly tokens per model, and daily billed dollars.
type Admin struct {
	Base   string
	Client *http.Client
	Now    func() time.Time

	adminKey, oauthToken string
	events               []Event
	billed               *Billed
}

// NewAdmin needs an admin key (sk-ant-admin...) or an org:admin OAuth token.
func NewAdmin(adminKey, oauthToken string) (*Admin, error) {
	if adminKey == "" && oauthToken == "" {
		return nil, errors.New("admin source needs ANTHROPIC_ADMIN_KEY or ANTHROPIC_ADMIN_OAUTH_TOKEN")
	}
	return &Admin{
		Base:       adminBase,
		Client:     &http.Client{Timeout: adminTimeout},
		Now:        time.Now,
		adminKey:   adminKey,
		oauthToken: oauthToken,
	}, nil
}

func (a *Admin) Label() string          { return "admin" }
func (a *Admin) HasRequestCounts() bool { return false } // tokens, not request counts
func (a *Admin) Events() []Event        { return a.events }
func (a *Admin) Billed() *Billed        { return a.billed }

type adminBucket struct {
	StartingAt string     `json:"starting_at"`
	Results    []adminRow `json:"results"`
}

type adminRow struct {
	Model                string                 `json:"model"`
	WorkspaceID          *string                `json:"workspace_id"`
	UncachedInputTokens  int64                  `json:"uncached_input_tokens"`
	CacheReadInputTokens int64                  `json:"cache_read_input_tokens"`
	CacheCreation        *pricing.CacheCreation `json:"cache_creation"`
	OutputTokens         int64                  `json:"output_tokens"`
	InferenceGeo         string                 `json:"inference_geo"`
	ServerToolUse        *struct {
		WebSearchRequests int64 `json:"web_search_requests"`
	} `json:"server_tool_use"`
	Amount json.RawMessage `json:"amount"` // decimal string, lowest currency unit (cents)
}

func (a *Admin) getAll(ctx context.Context, endpoint string, params [][2]string) ([]adminBucket, error) {
	var out []adminBucket
	page := ""
	for i := 0; i < adminMaxPages; i++ {
		parts := make([]string, 0, len(params)+1)
		for _, p := range params {
			parts = append(parts, url.QueryEscape(p[0])+"="+url.QueryEscape(p[1]))
		}
		if page != "" {
			parts = append(parts, "page="+url.QueryEscape(page))
		}
		req, err := http.NewRequestWithContext(ctx, http.MethodGet, a.Base+endpoint+"?"+strings.Join(parts, "&"), nil)
		if err != nil {
			return nil, err
		}
		req.Header.Set("anthropic-version", "2023-06-01")
		req.Header.Set("user-agent", adminUA)
		if a.oauthToken != "" {
			req.Header.Set("authorization", "Bearer "+a.oauthToken)
		} else {
			req.Header.Set("x-api-key", a.adminKey)
		}
		resp, err := a.Client.Do(req)
		if err != nil {
			return nil, err
		}
		body, err := io.ReadAll(resp.Body)
		resp.Body.Close()
		if resp.StatusCode < 200 || resp.StatusCode > 299 {
			text := string(body)
			if len(text) > adminErrBodyKeep {
				text = text[:adminErrBodyKeep]
			}
			return nil, fmt.Errorf("%s -> %s: %s", endpoint, resp.Status, text)
		}
		if err != nil {
			return nil, err
		}
		var j struct {
			Data     []adminBucket `json:"data"`
			HasMore  bool          `json:"has_more"`
			NextPage string        `json:"next_page"`
		}
		if err := json.Unmarshal(body, &j); err != nil {
			return nil, err
		}
		out = append(out, j.Data...)
		if !j.HasMore || j.NextPage == "" {
			break
		}
		page = j.NextPage
	}
	return out, nil
}

func (a *Admin) Refresh(ctx context.Context) ([]Event, error) {
	now := a.Now()
	usageStart := now.Add(-adminUsageHours * time.Hour)
	// The month is the local calendar's. Cost buckets are whole UTC days, so
	// it starts at the billing day dated the local 1st.
	monthStart := time.Date(now.Year(), now.Month(), 1, 0, 0, 0, 0, time.UTC)
	iso := func(t time.Time) string { return t.UTC().Format(adminIsoLayout) }

	var usageBuckets, costBuckets []adminBucket
	var usageErr, costErr error
	done := make(chan struct{})
	go func() {
		defer close(done)
		costBuckets, costErr = a.getAll(ctx, "/cost_report", [][2]string{
			{"starting_at", iso(monthStart)},
			{"ending_at", iso(now)},
			{"bucket_width", "1d"},
			{"limit", "31"},
			{"group_by[]", "description"},
		})
	}()
	usageBuckets, usageErr = a.getAll(ctx, "/usage_report/messages", [][2]string{
		{"starting_at", iso(usageStart)},
		{"ending_at", iso(now)},
		{"bucket_width", "1h"},
		{"limit", strconv.Itoa(adminUsageHours)},
		{"group_by[]", "model"},
		{"group_by[]", "workspace_id"},
		{"group_by[]", "inference_geo"},
	})
	<-done
	if usageErr != nil {
		return a.events, usageErr
	}
	if costErr != nil {
		return a.events, costErr
	}

	events := []Event{}
	for _, b := range usageBuckets {
		var t int64
		if ts, err := time.Parse(time.RFC3339, b.StartingAt); err == nil {
			t = ts.UnixMilli()
		}
		for _, r := range b.Results {
			cc := r.CacheCreation
			if cc == nil {
				cc = &pricing.CacheCreation{}
			}
			u := pricing.Usage{
				InputTokens:          r.UncachedInputTokens,
				CacheReadInputTokens: r.CacheReadInputTokens,
				CacheCreation:        cc,
				OutputTokens:         r.OutputTokens,
				InferenceGeo:         r.InferenceGeo,
			}
			project := "default"
			if r.WorkspaceID != nil {
				project = *r.WorkspaceID
			}
			var web int64
			if r.ServerToolUse != nil {
				web = r.ServerToolUse.WebSearchRequests
			}
			events = append(events, Event{
				T:          t,
				Model:      r.Model,
				Project:    project,
				Input:      u.InputTokens,
				CacheRead:  u.CacheReadInputTokens,
				CacheWrite: cc.Ephemeral5m + cc.Ephemeral1h,
				Output:     u.OutputTokens,
				WebSearch:  web,
				Cost:       pricing.CostUSD(r.Model, u),
			})
		}
	}

	byDay := map[string]float64{}
	total := 0.0
	for _, b := range costBuckets {
		day := b.StartingAt
		if len(day) > 10 {
			day = day[:10]
		}
		sum := 0.0
		for _, r := range b.Results {
			sum += amountCents(r.Amount) / 100
		}
		byDay[day] += sum
		total += sum
	}

	sort.SliceStable(events, func(i, j int) bool { return events[i].T < events[j].T })
	a.events = events
	a.billed = &Billed{ByDay: byDay, Total: total}
	return events, nil
}

// amountCents reads a JSON string or number; anything else counts as 0.
func amountCents(raw json.RawMessage) float64 {
	s := strings.Trim(strings.TrimSpace(string(raw)), `"`)
	f, err := strconv.ParseFloat(s, 64)
	if err != nil {
		return 0
	}
	return f
}
