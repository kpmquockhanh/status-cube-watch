package main

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"net/http"
	"strconv"
	"time"
)

const hookMaxStdin = 8 << 20

// runHook forwards one Claude Code hook event (stdin) to the running bridge.
// It never prints and never fails: a hook must not disturb Claude Code.
// Most events get hookTimeout. A turn's or session's end (endHooks) is waited
// on by nobody, and losing it leaves the tile running until the next turn, so
// it gets endTimeout and is posted again every endRetry while the bridge does
// not answer (a restart); applying one twice changes nothing.
func runHook(stdin io.Reader, port int) {
	body, _ := io.ReadAll(io.LimitReader(stdin, hookMaxStdin))
	timeout, retry := hookTimeout, false
	if endHooks[hookName(body)] {
		timeout, retry = endTimeout, true
	}
	deadline := time.Now().Add(timeout)
	for {
		if postHook(deadline, port, body) || !retry || time.Now().Add(endRetry).After(deadline) {
			return
		}
		time.Sleep(endRetry)
	}
}

// postHook posts body to /hook; true when the bridge answered.
func postHook(deadline time.Time, port int, body []byte) bool {
	ctx, cancel := context.WithDeadline(context.Background(), deadline)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, "http://127.0.0.1:"+strconv.Itoa(port)+"/hook", bytes.NewReader(body))
	if err != nil {
		return false
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-Cube-Client", "1")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return false
	}
	_, _ = io.Copy(io.Discard, io.LimitReader(resp.Body, 1<<16))
	resp.Body.Close()
	return true
}

// hookName is the event's hook_event_name; "" when body is not an event.
func hookName(body []byte) string {
	var ev struct {
		Name string `json:"hook_event_name"`
	}
	_ = json.Unmarshal(body, &ev)
	return ev.Name
}

// hookTimeout bounds what a hook can add to a Claude Code turn when the
// bridge is slow or wedged; a loopback POST normally takes about 1 ms.
var hookTimeout = 300 * time.Millisecond

// endHooks end a turn or a session: endTimeout and retries (runHook).
var endHooks = map[string]bool{"Stop": true, "SessionEnd": true}

var endTimeout, endRetry = 3 * time.Second, 250 * time.Millisecond
