package limits

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"sync/atomic"
	"testing"
	"time"
)

type fixture struct {
	r     *Reader
	srv   *httptest.Server
	hits  atomic.Int32
	now   time.Time
	token string
	h     func(w http.ResponseWriter, req *http.Request)
}

const okBody = `{"five_hour":{"utilization":42,"resets_at":"2026-10-24T20:00:00Z"},"seven_day":null}`

func newFixture(t *testing.T) *fixture {
	f := &fixture{now: time.Date(2026, 10, 24, 12, 0, 0, 0, time.UTC), token: "tok"}
	f.h = func(w http.ResponseWriter, _ *http.Request) { w.Write([]byte(okBody)) }
	f.srv = httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		f.hits.Add(1)
		f.h(w, req)
	}))
	t.Cleanup(f.srv.Close)
	f.r = &Reader{
		URL:    f.srv.URL,
		Client: &http.Client{Timeout: 2 * time.Second},
		Token:  func(context.Context) string { return f.token },
		Now:    func() time.Time { return f.now },
	}
	return f
}

func (f *fixture) get() Limits { return f.r.Get(context.Background()) }

func TestParseToken(t *testing.T) {
	for in, want := range map[string]string{
		`{"claudeAiOauth":{"accessToken":"a"}}`: "a",
		`{"accessToken":"b"}`:                   "b",
		`sk-ant-x`:                              "sk-ant-x",
		`garbage`:                               "",
		``:                                      "",
	} {
		if got := ParseToken(in); got != want {
			t.Errorf("ParseToken(%q)=%q want %q", in, got, want)
		}
	}
}

func TestOK(t *testing.T) {
	f := newFixture(t)
	f.h = func(w http.ResponseWriter, req *http.Request) {
		if req.Header.Get("Authorization") != "Bearer tok" || req.Header.Get("anthropic-beta") != "oauth-2025-04-20" || req.Header.Get("Accept") != "application/json" {
			t.Errorf("bad headers: %v", req.Header)
		}
		w.Write([]byte(okBody))
	}
	l := f.get()
	if l.FiveHour == nil || l.FiveHour.Utilization == nil || *l.FiveHour.Utilization != 42 || l.SevenDay != nil || l.Error != "" {
		t.Fatalf("got %+v", l)
	}
}

func TestCache60s(t *testing.T) {
	f := newFixture(t)
	f.get()
	f.now = f.now.Add(59 * time.Second)
	f.get()
	if f.hits.Load() != 1 {
		t.Fatalf("hits %d", f.hits.Load())
	}
	f.now = f.now.Add(2 * time.Second)
	f.get()
	if f.hits.Load() != 2 {
		t.Fatalf("hits %d", f.hits.Load())
	}
}

func TestNoLogin(t *testing.T) {
	f := newFixture(t)
	f.token = ""
	if l := f.get(); l.Error != "no Claude Code login found" || l.FiveHour != nil {
		t.Fatalf("got %+v", l)
	}
	f.token = "tok"
	f.now = f.now.Add(59 * time.Second)
	if l := f.get(); l.Error != "no Claude Code login found" {
		t.Fatalf("got %+v", l)
	}
	if f.hits.Load() != 0 {
		t.Fatal("request made before 60 s")
	}
	f.now = f.now.Add(2 * time.Second)
	if l := f.get(); l.Error != "" || f.hits.Load() != 1 {
		t.Fatalf("got %+v hits %d", l, f.hits.Load())
	}
}

func Test429Backoff(t *testing.T) {
	for _, tc := range []struct {
		retryAfter string
		wait       time.Duration
	}{{"60", 5 * time.Minute}, {"7200", 30 * time.Minute}, {"", 5 * time.Minute}, {"600", 10 * time.Minute}, {"99999999999", 30 * time.Minute}, {"1e300", 30 * time.Minute}} {
		f := newFixture(t)
		f.h = func(w http.ResponseWriter, _ *http.Request) {
			if tc.retryAfter != "" {
				w.Header().Set("Retry-After", tc.retryAfter)
			}
			w.WriteHeader(429)
		}
		if l := f.get(); l.Error != "usage endpoint 429" {
			t.Fatalf("got %+v", l)
		}
		f.now = f.now.Add(tc.wait - time.Second)
		f.get()
		if f.hits.Load() != 1 {
			t.Fatalf("%q: retried early", tc.retryAfter)
		}
		f.now = f.now.Add(2 * time.Second)
		f.get()
		if f.hits.Load() != 2 {
			t.Fatalf("%q: did not retry after %v", tc.retryAfter, tc.wait)
		}
	}
}

func TestOtherStatus(t *testing.T) {
	f := newFixture(t)
	f.h = func(w http.ResponseWriter, _ *http.Request) { w.WriteHeader(500) }
	if l := f.get(); l.Error != "usage endpoint 500" {
		t.Fatalf("got %+v", l)
	}
	f.now = f.now.Add(59 * time.Second)
	f.get()
	if f.hits.Load() != 1 {
		t.Fatalf("retried early")
	}
	f.now = f.now.Add(2 * time.Second)
	f.get()
	if f.hits.Load() != 2 {
		t.Fatalf("hits %d", f.hits.Load())
	}
}

func TestStaleThenEmpty(t *testing.T) {
	f := newFixture(t)
	f.get()
	f.h = func(w http.ResponseWriter, _ *http.Request) { w.WriteHeader(500) }
	f.now = f.now.Add(5 * time.Minute)
	l := f.get()
	if l.FiveHour == nil || *l.FiveHour.Utilization != 42 || l.Error != "usage endpoint 500" {
		t.Fatalf("at +5m got %+v", l)
	}
	f.now = f.now.Add(6 * time.Minute)
	l = f.get()
	if l.FiveHour != nil || l.SevenDay != nil || l.Error == "" {
		t.Fatalf("at +11m got %+v", l)
	}
}

func TestBadJSON(t *testing.T) {
	f := newFixture(t)
	f.h = func(w http.ResponseWriter, _ *http.Request) { w.Write([]byte("{")) }
	if l := f.get(); l.Error != "usage endpoint bad JSON" {
		t.Fatalf("got %+v", l)
	}
}

func TestTimeout(t *testing.T) {
	f := newFixture(t)
	f.r.Client = &http.Client{Timeout: 50 * time.Millisecond}
	f.h = func(w http.ResponseWriter, req *http.Request) {
		select {
		case <-time.After(500 * time.Millisecond):
		case <-req.Context().Done():
		}
	}
	if l := f.get(); l.Error != "usage endpoint timeout" {
		t.Fatalf("got %+v", l)
	}
}

// A window that is not an object reads as no reading (a non-nil Window with
// nil Utilization, which cards treats as empty); the rest still decodes.
func TestNonObjectWindow(t *testing.T) {
	f := newFixture(t)
	f.h = func(w http.ResponseWriter, _ *http.Request) {
		w.Write([]byte(`{"five_hour":5,"seven_day":{"utilization":10,"resets_at":"2026-10-24T20:00:00Z"}}`))
	}
	l := f.get()
	if l.Error != "" || l.FiveHour == nil || l.FiveHour.Utilization != nil || l.SevenDay == nil || *l.SevenDay.Utilization != 10 {
		t.Fatalf("got %+v", l)
	}
	var w Window
	if err := json.Unmarshal([]byte(`"x"`), &w); err != nil || w.Utilization != nil {
		t.Fatalf("string window: %v %+v", err, w)
	}
}
