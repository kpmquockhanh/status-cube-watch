package main

import (
	"bytes"
	"io"
	"net/http"
	"strconv"
	"time"
)

const hookMaxStdin = 8 << 20

// runHook forwards one Claude Code hook event (stdin) to the running bridge.
// It never prints and never fails: a hook must not disturb Claude Code.
func runHook(stdin io.Reader, port int, client *http.Client) {
	body, _ := io.ReadAll(io.LimitReader(stdin, hookMaxStdin))
	req, err := http.NewRequest(http.MethodPost, "http://127.0.0.1:"+strconv.Itoa(port)+"/hook", bytes.NewReader(body))
	if err != nil {
		return
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-Cube-Client", "1")
	resp, err := client.Do(req)
	if err != nil {
		return
	}
	_, _ = io.Copy(io.Discard, io.LimitReader(resp.Body, 1<<16))
	resp.Body.Close()
}

func hookClient() *http.Client { return &http.Client{Timeout: time.Second} }
