package main

import (
	"bytes"
	"strings"
	"testing"
)

func TestArgs(t *testing.T) {
	var buf bytes.Buffer
	c, code := parseCommand([]string{"hook", "--port", "9000"}, &buf)
	if code != -1 || c.kind != "hook" || c.port != 9000 {
		t.Fatalf("hook: %+v %d", c, code)
	}
	if c, code = parseCommand([]string{"hook"}, &buf); code != -1 || c.port != 8787 {
		t.Fatalf("hook default: %+v %d", c, code)
	}
	if _, code = parseCommand([]string{"hook", "--port", "x"}, &buf); code != 2 {
		t.Fatalf("bad port: %d", code)
	}
	if _, code = parseCommand([]string{"hook", "extra"}, &buf); code != 2 {
		t.Fatalf("hook extra: %d", code)
	}
	buf.Reset()
	if _, code = parseCommand([]string{"banana"}, &buf); code != 2 || !strings.Contains(buf.String(), "usage") {
		t.Fatalf("banana: %d %q", code, buf.String())
	}
	if c, code = parseCommand(nil, &buf); code != -1 || c.kind != "serve" || c.config != "config.json" {
		t.Fatalf("serve: %+v %d", c, code)
	}
	if c, code = parseCommand([]string{"-config", "x.json"}, &buf); code != -1 || c.kind != "serve" || c.config != "x.json" {
		t.Fatalf("serve -config: %+v %d", c, code)
	}
}

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
