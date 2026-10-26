package server

import (
	"encoding/json"
	"net/http"
	"strings"
	"testing"

	"claude-status-cube/bridge/internal/cards"
)

// boardReq is a request from the web board: the browser on this Mac, at
// http://127.0.0.1:8787/board/.
func boardReq(method, path, body string) *http.Request {
	r := cubeReq(method, path, body)
	r.Header.Set("Origin", "http://127.0.0.1:8787")
	r.Header.Set("Sec-Fetch-Site", "same-origin")
	return r
}

func TestBoardGuard(t *testing.T) {
	for _, tc := range []struct {
		name, remote, host, origin, site string
		want                             bool
	}{
		{"no origin", "127.0.0.1:5000", "127.0.0.1:8787", "", "", true},
		{"own origin", "127.0.0.1:5000", "127.0.0.1:8787", "http://127.0.0.1:8787", "same-origin", true},
		{"own origin, localhost", "[::1]:5000", "localhost:8787", "http://localhost:8787", "", true},
		{"other page", "127.0.0.1:5000", "127.0.0.1:8787", "http://evil.test", "cross-site", false},
		{"other port", "127.0.0.1:5000", "127.0.0.1:8787", "http://127.0.0.1:3000", "same-site", false},
		{"other name for the host", "127.0.0.1:5000", "127.0.0.1:8787", "http://localhost:8787", "", false},
		{"null origin", "127.0.0.1:5000", "127.0.0.1:8787", "null", "", false},
		{"own origin, cross site", "127.0.0.1:5000", "127.0.0.1:8787", "http://127.0.0.1:8787", "cross-site", false},
		{"dns rebinding", "127.0.0.1:5000", "evil.test:8787", "http://evil.test:8787", "same-origin", false},
		{"lan", "192.168.1.20:5000", "192.168.1.5:8787", "http://192.168.1.5:8787", "same-origin", false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			r := req("GET", "/api/board", tc.remote, tc.host, "")
			if tc.origin != "" {
				r.Header.Set("Origin", tc.origin)
			}
			if tc.site != "" {
				r.Header.Set("Sec-Fetch-Site", tc.site)
			}
			if got := boardRequest(r, 8787); got != tc.want {
				t.Fatalf("boardRequest = %v, want %v", got, tc.want)
			}
		})
	}
	r := req("GET", "/", "127.0.0.1:5000", "127.0.0.1:8787", "")
	r.Header["Origin"] = []string{"http://127.0.0.1:8787", "http://127.0.0.1:8787"}
	if boardRequest(r, 8787) {
		t.Fatal("two Origin headers passed the guard")
	}
}

func TestBoardEndpoint(t *testing.T) {
	s := newAgentsTest(newStubAgents(waitingAgent()), nil)
	rec := serve(s, boardReq("GET", "/api/board", ""))
	var b cards.Board
	if rec.Code != 200 || json.Unmarshal(rec.Body.Bytes(), &b) != nil {
		t.Fatalf("board: %d %s", rec.Code, rec.Body)
	}
	if len(b.Agents) != 1 || b.Agents[0].Q == nil || b.Agents[0].Q.T != secretTitle || b.Agents[0].D == nil {
		t.Fatalf("board = %s", rec.Body)
	}

	cross := boardReq("GET", "/api/board", "")
	cross.Header.Set("Origin", "http://evil.test")
	lan := req("GET", "/api/board", "192.168.1.20:5000", "192.168.1.5:8787", "")
	for name, r := range map[string]*http.Request{"cross origin": cross, "lan": lan} {
		rec := serve(s, r)
		if rec.Code != 404 || strings.Contains(rec.Body.String(), "secret") {
			t.Fatalf("%s: %d %s", name, rec.Code, rec.Body)
		}
	}
	if rec := serve(newAgentsTest(nil, nil), boardReq("GET", "/api/board", "")); rec.Code != 404 ||
		!strings.Contains(rec.Body.String(), "agents are off") {
		t.Fatalf("agents off: %d %s", rec.Code, rec.Body)
	}
}

func TestBoardAnswers(t *testing.T) {
	ag := newStubAgents(waitingAgent())
	s := newAgentsTest(ag, nil)
	if rec := serve(s, boardReq("POST", "/api/answer", answerBody("a1", "p1", "1"))); rec.Code != 204 {
		t.Fatalf("board answer: %d %s", rec.Code, rec.Body)
	}
	// The board sends X-Cube-Client like the Mac app; without it, no answer.
	noHeader := boardReq("POST", "/api/answer", answerBody("a1", "p1", "2"))
	noHeader.Header.Del("X-Cube-Client")
	if rec := serve(s, noHeader); rec.Code != 404 {
		t.Fatalf("no header: %d", rec.Code)
	}
	// /hook stays the hook command's: the board's origin is not let in.
	if rec := serve(s, boardReq("POST", "/hook", `{}`)); rec.Code != 404 {
		t.Fatalf("hook from the board: %d", rec.Code)
	}
	if _, n := ag.counts(); n != 1 {
		t.Fatalf("Answer called %d times, want 1", n)
	}
}

func TestBoardFiles(t *testing.T) {
	s := newAgentsTest(nil, nil)
	for path, typ := range map[string]string{
		"/board": "text/html", "/board/": "text/html", "/board/index.html": "text/html",
		"/board/board.js": "text/javascript", "/board/critter.js": "text/javascript", "/board/board.css": "text/css",
	} {
		rec := serve(s, req("GET", path, "192.168.1.20:5000", "192.168.1.5:8787", ""))
		if rec.Code != 200 || !strings.HasPrefix(rec.Header().Get("Content-Type"), typ) || rec.Body.Len() == 0 {
			t.Fatalf("%s: %d %q", path, rec.Code, rec.Header().Get("Content-Type"))
		}
		if !strings.Contains(rec.Header().Get("Content-Security-Policy"), "frame-ancestors 'none'") {
			t.Fatalf("%s: CSP %q", path, rec.Header().Get("Content-Security-Policy"))
		}
	}
	if rec := serve(s, req("GET", "/board/../config.json", "127.0.0.1:5000", "127.0.0.1:8787", "")); rec.Code != 404 {
		t.Fatalf("other file: %d", rec.Code)
	}
}
