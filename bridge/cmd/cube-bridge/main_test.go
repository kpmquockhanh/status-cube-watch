package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"claude-status-cube/bridge/internal/hooksettings"
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

func TestHooksArgs(t *testing.T) {
	var buf bytes.Buffer
	c, code := parseCommand([]string{"hooks", "install", "--settings", "/tmp/x.json", "--port", "9000"}, &buf)
	if code != -1 || c.kind != "hooks" || c.action != "install" || c.settings != "/tmp/x.json" || c.port != 9000 {
		t.Fatalf("install: %+v %d", c, code)
	}
	home, err := os.UserHomeDir()
	if err != nil {
		t.Fatal(err)
	}
	c, code = parseCommand([]string{"hooks", "uninstall"}, &buf)
	if code != -1 || c.action != "uninstall" || c.port != 8787 || c.settings != filepath.Join(home, ".claude", "settings.json") {
		t.Fatalf("uninstall defaults: %+v %d", c, code)
	}
	for _, args := range [][]string{{"hooks"}, {"hooks", "frob"}, {"hooks", "install", "extra"}, {"hooks", "install", "--port", "0"}, {"hooks", "install", "--settings", ""}} {
		buf.Reset()
		if _, code := parseCommand(args, &buf); code != 2 || !strings.Contains(buf.String(), "usage") {
			t.Fatalf("%v: %d %q", args, code, buf.String())
		}
	}
}

func TestMainHooksInstall(t *testing.T) {
	settings := filepath.Join(t.TempDir(), "settings.json")
	var stderr, stdout bytes.Buffer
	c, code := parseCommand([]string{"hooks", "install", "--settings", settings, "--port", "9000"}, &stderr)
	if code != -1 {
		t.Fatalf("parse: %d %s", code, stderr.String())
	}
	if code := runHooks(c, &stdout, &stderr); code != 0 {
		t.Fatalf("install: %d %s", code, stderr.String())
	}
	exe, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	if exe, err = filepath.EvalSymlinks(exe); err != nil {
		t.Fatal(err)
	}
	b, err := os.ReadFile(settings)
	if err != nil {
		t.Fatal(err)
	}
	want := hooksettings.Command(exe, 9000)
	if !filepath.IsAbs(exe) || !strings.Contains(want, "hook --port 9000") {
		t.Fatalf("command %q", want)
	}
	var doc struct {
		Hooks map[string][]struct {
			Hooks []struct{ Command string } `json:"hooks"`
		} `json:"hooks"`
	}
	if err := json.Unmarshal(b, &doc); err != nil {
		t.Fatal(err)
	}
	for _, ev := range hooksettings.Events {
		g := doc.Hooks[ev]
		if len(g) != 1 || len(g[0].Hooks) != 1 || g[0].Hooks[0].Command != want {
			t.Fatalf("%s: %+v want %q", ev, g, want)
		}
	}
	if !strings.Contains(stdout.String(), settings) {
		t.Fatalf("stdout: %q", stdout.String())
	}
	c.action = "uninstall"
	if code := runHooks(c, &stdout, &stderr); code != 0 {
		t.Fatalf("uninstall: %d %s", code, stderr.String())
	}
	if b, _ := os.ReadFile(settings); strings.Contains(string(b), "hook --port") {
		t.Fatalf("still installed: %s", b)
	}
}

func TestMainHooksBrokenFile(t *testing.T) {
	settings := filepath.Join(t.TempDir(), "settings.json")
	if err := os.WriteFile(settings, []byte(`{"hooks": `), 0o644); err != nil {
		t.Fatal(err)
	}
	var stdout, stderr bytes.Buffer
	c := command{kind: "hooks", action: "install", settings: settings, port: 8787}
	if code := runHooks(c, &stdout, &stderr); code != 1 || !strings.Contains(stderr.String(), settings) {
		t.Fatalf("%d %q", code, stderr.String())
	}
}
