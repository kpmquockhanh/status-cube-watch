package limits

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	usageURL        = "https://api.anthropic.com/api/oauth/usage"
	cacheFor        = 60 * time.Second
	timeout         = 4 * time.Second
	keychainTimeout = 3 * time.Second
	maxStale        = 10 * time.Minute
	backoff         = 5 * time.Minute
	maxBackoff      = 30 * time.Minute
)

// Reader fetches the usage windows, caching and failing soft, as the Node bridge did.
type Reader struct {
	URL    string
	Client *http.Client
	Token  func(ctx context.Context) string
	Now    func() time.Time

	mu        sync.Mutex
	cacheAt   time.Time
	cacheData *Limits
	retryAt   time.Time
	lastError string
}

// NewReader returns a Reader with the production defaults.
func NewReader() *Reader {
	return &Reader{
		URL:    usageURL,
		Client: &http.Client{Timeout: timeout},
		Token:  defaultToken,
		Now:    time.Now,
	}
}

// ParseToken extracts an access token from the credentials JSON or a bare
// "sk-" token; "" when there is none.
func ParseToken(raw string) string {
	if raw == "" {
		return ""
	}
	var c struct {
		ClaudeAiOauth *struct {
			AccessToken string `json:"accessToken"`
		} `json:"claudeAiOauth"`
		AccessToken string `json:"accessToken"`
	}
	if err := json.Unmarshal([]byte(raw), &c); err != nil {
		if strings.HasPrefix(raw, "sk-") {
			return raw
		}
		return ""
	}
	if c.ClaudeAiOauth != nil && c.ClaudeAiOauth.AccessToken != "" {
		return c.ClaudeAiOauth.AccessToken
	}
	return c.AccessToken
}

func defaultToken(ctx context.Context) string {
	if runtime.GOOS == "darwin" {
		kctx, cancel := context.WithTimeout(ctx, keychainTimeout)
		defer cancel()
		out, err := exec.CommandContext(kctx, "security", "find-generic-password", "-s", "Claude Code-credentials", "-w").Output()
		if err == nil {
			if t := ParseToken(strings.TrimSpace(string(out))); t != "" {
				return t
			}
		}
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return ""
	}
	b, err := os.ReadFile(filepath.Join(home, ".claude", ".credentials.json"))
	if err != nil {
		return ""
	}
	return ParseToken(string(b))
}

// failed keeps the last good reading for up to maxStale, tagged with the
// error; past that an old percentage is a guess, so both windows are empty.
func (r *Reader) failed(now time.Time, msg string) Limits {
	if r.cacheData != nil && now.Sub(r.cacheAt) < maxStale {
		l := *r.cacheData
		l.Error = msg
		return l
	}
	return Limits{Error: msg}
}

// Get returns the current limits, from cache when fresh.
func (r *Reader) Get(ctx context.Context) Limits {
	r.mu.Lock()
	defer r.mu.Unlock()
	now := r.Now()
	if r.cacheData != nil && now.Sub(r.cacheAt) < cacheFor {
		return *r.cacheData
	}
	if now.Before(r.retryAt) {
		return r.failed(now, r.lastError)
	}
	fail := func(msg string, wait time.Duration) Limits {
		r.lastError = msg
		r.retryAt = now.Add(wait)
		return r.failed(now, msg)
	}

	token := r.Token(ctx)
	if token == "" {
		return fail("no Claude Code login found", cacheFor)
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, r.URL, nil)
	if err != nil {
		return fail(err.Error(), cacheFor)
	}
	req.Header.Set("Accept", "application/json")
	req.Header.Set("Authorization", "Bearer "+token)
	req.Header.Set("anthropic-beta", "oauth-2025-04-20")
	res, err := r.Client.Do(req)
	if err != nil {
		return fail(errMessage(err, "fetch failed"), cacheFor)
	}
	defer res.Body.Close()
	if res.StatusCode < 200 || res.StatusCode > 299 {
		wait := cacheFor
		if res.StatusCode == http.StatusTooManyRequests {
			wait = backoff
			if s, err := strconv.ParseFloat(res.Header.Get("Retry-After"), 64); err == nil && s > 0 {
				wait = time.Duration(s * float64(time.Second))
			}
			wait = min(max(wait, backoff), maxBackoff)
		}
		return fail(fmt.Sprintf("usage endpoint %d", res.StatusCode), wait)
	}
	var body struct {
		FiveHour *Window `json:"five_hour"`
		SevenDay *Window `json:"seven_day"`
	}
	if err := json.NewDecoder(res.Body).Decode(&body); err != nil {
		return fail(errMessage(err, "usage endpoint bad JSON"), cacheFor)
	}
	data := Limits{FiveHour: body.FiveHour, SevenDay: body.SevenDay}
	r.cacheAt, r.cacheData, r.retryAt = now, &data, time.Time{}
	return data
}

// errMessage maps a timeout to Node's message and anything else to other.
func errMessage(err error, other string) string {
	var ne net.Error
	if errors.Is(err, context.DeadlineExceeded) || (errors.As(err, &ne) && ne.Timeout()) {
		return "usage endpoint timeout"
	}
	return other
}
