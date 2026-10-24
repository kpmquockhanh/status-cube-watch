package sources

import (
	"context"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"
)

var adminNow = time.Date(2026, 10, 24, 12, 0, 0, 0, time.UTC)

func newTestAdmin(t *testing.T, key, token string, h http.HandlerFunc) *Admin {
	t.Helper()
	srv := httptest.NewServer(h)
	t.Cleanup(srv.Close)
	a, err := NewAdmin(key, token)
	if err != nil {
		t.Fatal(err)
	}
	a.Base = srv.URL
	a.Now = func() time.Time { return adminNow }
	return a
}

func emptyReports(w http.ResponseWriter, r *http.Request) {
	w.Write([]byte(`{"data":[],"has_more":false}`))
}

func TestNewAdminNeedsCredential(t *testing.T) {
	_, err := NewAdmin("", "")
	if err == nil || err.Error() != "admin source needs ANTHROPIC_ADMIN_KEY or ANTHROPIC_ADMIN_OAUTH_TOKEN" {
		t.Fatalf("err = %v", err)
	}
}

func TestHeaders(t *testing.T) {
	for _, c := range []struct{ key, token string }{{"sk-admin", ""}, {"", "tok"}} {
		var mu sync.Mutex
		var got []http.Header
		a := newTestAdmin(t, c.key, c.token, func(w http.ResponseWriter, r *http.Request) {
			mu.Lock()
			got = append(got, r.Header.Clone())
			mu.Unlock()
			emptyReports(w, r)
		})
		if _, err := a.Refresh(context.Background()); err != nil {
			t.Fatal(err)
		}
		if len(got) != 2 {
			t.Fatalf("requests = %d", len(got))
		}
		for _, h := range got {
			if h.Get("anthropic-version") != "2023-06-01" || h.Get("user-agent") != "claude-status-cube/1.0" {
				t.Errorf("headers = %v", h)
			}
			if c.token != "" {
				if h.Get("Authorization") != "Bearer tok" || h.Get("x-api-key") != "" {
					t.Errorf("oauth headers = %v", h)
				}
			} else if h.Get("x-api-key") != "sk-admin" || h.Get("Authorization") != "" {
				t.Errorf("key headers = %v", h)
			}
		}
	}
}

func TestUsageQuery(t *testing.T) {
	var usage, cost string
	var mu sync.Mutex
	a := newTestAdmin(t, "k", "", func(w http.ResponseWriter, r *http.Request) {
		mu.Lock()
		if strings.HasSuffix(r.URL.Path, "/usage_report/messages") {
			usage = r.URL.RawQuery
		} else {
			cost = r.URL.RawQuery
		}
		mu.Unlock()
		emptyReports(w, r)
	})
	if _, err := a.Refresh(context.Background()); err != nil {
		t.Fatal(err)
	}
	want := "starting_at=2026-10-17T12%3A00%3A00.000Z&ending_at=2026-10-24T12%3A00%3A00.000Z&bucket_width=1h&limit=168" +
		"&group_by%5B%5D=model&group_by%5B%5D=workspace_id&group_by%5B%5D=inference_geo"
	if usage != want {
		t.Errorf("usage query\n got %s\nwant %s", usage, want)
	}
	wantCost := "starting_at=2026-10-01T00%3A00%3A00.000Z&ending_at=2026-10-24T12%3A00%3A00.000Z&bucket_width=1d&limit=31&group_by%5B%5D=description"
	if cost != wantCost {
		t.Errorf("cost query\n got %s\nwant %s", cost, wantCost)
	}
}

func TestCostStartsAtLocalFirstAsUTCMidnight(t *testing.T) {
	var cost string
	a := newTestAdmin(t, "k", "", func(w http.ResponseWriter, r *http.Request) {
		if strings.HasSuffix(r.URL.Path, "/cost_report") {
			cost = r.URL.Query().Get("starting_at")
		}
		emptyReports(w, r)
	})
	// 2026-11-01 02:00 at UTC+10 is still Oct 31 in UTC: the local month is November.
	a.Now = func() time.Time { return time.Date(2026, 11, 1, 2, 0, 0, 0, time.FixedZone("x", 10*3600)) }
	if _, err := a.Refresh(context.Background()); err != nil {
		t.Fatal(err)
	}
	if cost != "2026-11-01T00:00:00.000Z" {
		t.Errorf("starting_at = %s", cost)
	}
}

func TestPagination(t *testing.T) {
	var mu sync.Mutex
	var pages []string
	a := newTestAdmin(t, "k", "", func(w http.ResponseWriter, r *http.Request) {
		if !strings.HasSuffix(r.URL.Path, "/usage_report/messages") {
			emptyReports(w, r)
			return
		}
		mu.Lock()
		pages = append(pages, r.URL.Query().Get("page"))
		mu.Unlock()
		if r.URL.Query().Get("page") == "" {
			w.Write([]byte(`{"data":[{"starting_at":"2026-10-24T10:00:00Z","results":[{"model":"claude-sonnet-4-5","output_tokens":1}]}],"has_more":true,"next_page":"p2"}`))
			return
		}
		w.Write([]byte(`{"data":[{"starting_at":"2026-10-24T09:00:00Z","results":[{"model":"claude-sonnet-4-5","output_tokens":2}]}],"has_more":false}`))
	})
	ev, err := a.Refresh(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if len(ev) != 2 || ev[0].Output != 2 || ev[1].Output != 1 {
		t.Fatalf("events = %+v (want sorted by T)", ev)
	}
	if strings.Join(pages, ",") != ",p2" {
		t.Errorf("pages = %q", pages)
	}
}

func TestPaginationStopsAt20(t *testing.T) {
	var mu sync.Mutex
	n := 0
	a := newTestAdmin(t, "k", "", func(w http.ResponseWriter, r *http.Request) {
		if strings.HasSuffix(r.URL.Path, "/usage_report/messages") {
			mu.Lock()
			n++
			mu.Unlock()
			w.Write([]byte(`{"data":[],"has_more":true,"next_page":"again"}`))
			return
		}
		emptyReports(w, r)
	})
	if _, err := a.Refresh(context.Background()); err != nil {
		t.Fatal(err)
	}
	if n != 20 {
		t.Errorf("pages fetched = %d", n)
	}
}

func TestEventsAndBilled(t *testing.T) {
	a := newTestAdmin(t, "k", "", func(w http.ResponseWriter, r *http.Request) {
		if strings.HasSuffix(r.URL.Path, "/cost_report") {
			w.Write([]byte(`{"data":[
				{"starting_at":"2026-10-23T00:00:00Z","results":[{"amount":"1234"},{"amount":"66"}]},
				{"starting_at":"2026-10-24T00:00:00Z","results":[{"amount":"500"},{}]}]}`))
			return
		}
		w.Write([]byte(`{"data":[{"starting_at":"2026-10-24T10:00:00Z","results":[
			{"model":"claude-sonnet-4-5","workspace_id":"wrkspc_1","uncached_input_tokens":1000000,"cache_read_input_tokens":0,
			 "cache_creation":{"ephemeral_5m_input_tokens":3,"ephemeral_1h_input_tokens":4},"output_tokens":0,
			 "server_tool_use":{"web_search_requests":2}},
			{"model":"claude-sonnet-4-5","workspace_id":null,"uncached_input_tokens":1000000,"inference_geo":"us"}]}]}`))
	})
	if a.Billed() != nil {
		t.Fatal("Billed before refresh must be nil")
	}
	ev, err := a.Refresh(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if len(ev) != 2 {
		t.Fatalf("events = %+v", ev)
	}
	e0, e1 := ev[0], ev[1]
	if e0.Project != "wrkspc_1" || e1.Project != "default" || e0.Session != "" {
		t.Errorf("project/session = %+v %+v", e0, e1)
	}
	if e0.T != time.Date(2026, 10, 24, 10, 0, 0, 0, time.UTC).UnixMilli() {
		t.Errorf("T = %d", e0.T)
	}
	if e0.Input != 1000000 || e0.CacheWrite != 7 || e0.WebSearch != 2 {
		t.Errorf("e0 = %+v", e0)
	}
	if e0.Cost <= 0 || e1.Cost <= e0.Cost*1.09 || e1.Cost >= e0.Cost*1.11 {
		t.Errorf("costs = %v %v (want us geo 1.1x)", e0.Cost, e1.Cost)
	}
	b := a.Billed()
	if b == nil || b.ByDay["2026-10-23"] != 13.0 || b.ByDay["2026-10-24"] != 5.0 || b.Total != 18.0 {
		t.Errorf("billed = %+v", b)
	}
	if a.Label() != "admin" || a.HasRequestCounts() {
		t.Error("label/hasRequestCounts")
	}
}

func TestCostAmountSum(t *testing.T) {
	a := newTestAdmin(t, "k", "", func(w http.ResponseWriter, r *http.Request) {
		if strings.HasSuffix(r.URL.Path, "/cost_report") {
			w.Write([]byte(`{"data":[{"starting_at":"2026-10-24T00:00:00Z","results":[{"amount":"1234"}]}]}`))
			return
		}
		emptyReports(w, r)
	})
	if _, err := a.Refresh(context.Background()); err != nil {
		t.Fatal(err)
	}
	if got := a.Billed().ByDay["2026-10-24"]; got != 12.34 {
		t.Errorf("day = %v", got)
	}
}

func TestHTTPError(t *testing.T) {
	a := newTestAdmin(t, "k", "", func(w http.ResponseWriter, r *http.Request) {
		if strings.HasSuffix(r.URL.Path, "/usage_report/messages") {
			http.Error(w, "bad key", http.StatusUnauthorized)
			return
		}
		emptyReports(w, r)
	})
	_, err := a.Refresh(context.Background())
	if err == nil || !strings.HasPrefix(err.Error(), "/usage_report/messages -> 401") || !strings.Contains(err.Error(), "bad key") {
		t.Fatalf("err = %v", err)
	}
	if a.Billed() != nil {
		t.Error("failed refresh must not set Billed")
	}
}
