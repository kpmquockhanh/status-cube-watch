package main

import (
	"bytes"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strconv"
	"strings"
	"testing"
	"time"
)

type hookCapture struct {
	body   []byte
	header http.Header
	path   string
}

func hookServer(t *testing.T) (port int, got chan hookCapture) {
	t.Helper()
	got = make(chan hookCapture, 1)
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		b, _ := io.ReadAll(r.Body)
		got <- hookCapture{b, r.Header.Clone(), r.URL.Path}
	}))
	t.Cleanup(srv.Close)
	_, p, _ := net.SplitHostPort(srv.Listener.Addr().String())
	port, _ = strconv.Atoi(p)
	return port, got
}

func TestHookPosts(t *testing.T) {
	port, got := hookServer(t)
	in := `{"hook_event_name":"Stop","session_id":"s1"}`
	runHook(strings.NewReader(in), port)
	c := <-got
	if c.path != "/hook" || string(c.body) != in {
		t.Fatalf("path %q body %q", c.path, c.body)
	}
	if c.header.Get("X-Cube-Client") != "1" || c.header.Get("Content-Type") != "application/json" {
		t.Fatalf("headers %v", c.header)
	}
	if c.header.Get("Origin") != "" {
		t.Fatalf("Origin set")
	}
}

func TestHookReadsLargeStdin(t *testing.T) {
	port, got := hookServer(t)
	runHook(bytes.NewReader(bytes.Repeat([]byte("a"), 3<<20)), port)
	if c := <-got; len(c.body) != 3<<20 {
		t.Fatalf("3 MB arrived as %d", len(c.body))
	}
	runHook(bytes.NewReader(bytes.Repeat([]byte("a"), 9<<20)), port)
	if c := <-got; len(c.body) != 8<<20 {
		t.Fatalf("9 MB arrived as %d", len(c.body))
	}
}

func TestHookBridgeDown(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	_, p, _ := net.SplitHostPort(l.Addr().String())
	l.Close()
	port, _ := strconv.Atoi(p)
	start := time.Now()
	runHook(strings.NewReader("{}"), port)
	if d := time.Since(start); d > 600*time.Millisecond {
		t.Fatalf("took %v", d)
	}
}

func TestHookSlowBridge(t *testing.T) {
	release := make(chan struct{})
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { <-release }))
	defer srv.Close()
	defer close(release)
	_, p, _ := net.SplitHostPort(srv.Listener.Addr().String())
	port, _ := strconv.Atoi(p)
	start := time.Now()
	runHook(strings.NewReader("{}"), port)
	if d := time.Since(start); d > 600*time.Millisecond {
		t.Fatalf("took %v", d)
	}
}

// A turn's end is posted again while the bridge is down, so one that restarts
// meanwhile still gets it; other events are not.
func TestHookEndRetriesUntilTheBridgeAnswers(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	addr := l.Addr().String()
	_, p, _ := net.SplitHostPort(addr)
	port, _ := strconv.Atoi(p)
	l.Close() // down for now

	got := make(chan string, 4)
	go func() {
		time.Sleep(600 * time.Millisecond) // the bridge comes back
		l, err := net.Listen("tcp", addr)
		if err != nil {
			got <- "listen: " + err.Error()
			return
		}
		srv := &http.Server{Handler: http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			b, _ := io.ReadAll(r.Body)
			got <- string(b)
		})}
		t.Cleanup(func() { srv.Close() })
		_ = srv.Serve(l)
	}()

	start := time.Now()
	runHook(strings.NewReader(`{"hook_event_name":"PreToolUse","session_id":"s1"}`), port)
	if d := time.Since(start); d > 400*time.Millisecond {
		t.Fatalf("a tool event waited %v", d)
	}
	in := `{"hook_event_name":"Stop","session_id":"s1"}`
	runHook(strings.NewReader(in), port)
	if b := <-got; b != in {
		t.Fatalf("got %q", b)
	}
	if d := time.Since(start); d > endTimeout {
		t.Fatalf("took %v", d)
	}
	select {
	case b := <-got:
		t.Fatalf("posted again after an answer: %q", b)
	case <-time.After(300 * time.Millisecond):
	}
}

// With the bridge down for good, a turn's end gives up at endTimeout.
func TestHookEndGivesUp(t *testing.T) {
	defer func(d, r time.Duration) { endTimeout, endRetry = d, r }(endTimeout, endRetry)
	endTimeout, endRetry = 500*time.Millisecond, 100*time.Millisecond
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	_, p, _ := net.SplitHostPort(l.Addr().String())
	l.Close()
	port, _ := strconv.Atoi(p)
	start := time.Now()
	runHook(strings.NewReader(`{"hook_event_name":"SessionEnd","session_id":"s1"}`), port)
	if d := time.Since(start); d < 300*time.Millisecond || d > 800*time.Millisecond {
		t.Fatalf("took %v", d)
	}
}
