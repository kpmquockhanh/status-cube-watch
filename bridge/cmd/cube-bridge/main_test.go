package main

import (
	"bytes"
	"strings"
	"testing"
)

func TestParseArgs(t *testing.T) {
	var buf bytes.Buffer
	if p, code := parseArgs(nil, &buf); code != -1 || p != "config.json" {
		t.Fatalf("default: %q %d", p, code)
	}
	if p, code := parseArgs([]string{"-config", "x.json"}, &buf); code != -1 || p != "x.json" {
		t.Fatalf("-config: %q %d", p, code)
	}
	buf.Reset()
	if _, code := parseArgs([]string{"serve"}, &buf); code != 2 || !strings.Contains(buf.String(), "usage: cube-bridge") {
		t.Fatalf("positional: %d %q", code, buf.String())
	}
	buf.Reset()
	if _, code := parseArgs([]string{"-nope"}, &buf); code != 2 {
		t.Fatalf("bad flag: %d", code)
	}
}
