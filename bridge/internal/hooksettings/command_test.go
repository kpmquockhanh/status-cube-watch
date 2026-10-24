package hooksettings

import (
	"bytes"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestCommandShape(t *testing.T) {
	got := Command("/a/b", 8787)
	want := "[ -x '/a/b' ] && '/a/b' hook --port 8787 >/dev/null 2>&1; exit 0"
	if got != want {
		t.Fatalf("got %q", got)
	}
}

func TestCommandQuotingRoundTrips(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "it's a dir")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatal(err)
	}
	bin := filepath.Join(dir, "cube bridge")
	body := "#!/bin/sh\necho \"$@\" > \"$(dirname \"$0\")/args.txt\"\n"
	if err := os.WriteFile(bin, []byte(body), 0o755); err != nil {
		t.Fatal(err)
	}
	var so, se bytes.Buffer
	cmd := exec.Command("/bin/sh", "-c", Command(bin, 9001))
	cmd.Stdout, cmd.Stderr = &so, &se
	if err := cmd.Run(); err != nil {
		t.Fatal(err)
	}
	if so.Len() != 0 || se.Len() != 0 {
		t.Fatalf("output: %q %q", so.String(), se.String())
	}
	b, err := os.ReadFile(filepath.Join(dir, "args.txt"))
	if err != nil || string(b) != "hook --port 9001\n" {
		t.Fatalf("args file: %q %v", b, err)
	}
}

func TestCommandBinaryMissing(t *testing.T) {
	var so, se bytes.Buffer
	cmd := exec.Command("/bin/sh", "-c", Command("/nonexistent/cube-bridge", 8787))
	cmd.Stdout, cmd.Stderr = &so, &se
	if err := cmd.Run(); err != nil {
		t.Fatalf("exit: %v", err)
	}
	if so.Len() != 0 || se.Len() != 0 {
		t.Fatalf("output: %q %q", so.String(), se.String())
	}
}
