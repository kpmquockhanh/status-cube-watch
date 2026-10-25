package hatstore

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func ip(n int) *int { return &n }

func TestSetGetPersist(t *testing.T) {
	path := filepath.Join(t.TempDir(), "sub", "hats.json")
	s := Open(path, nil)
	if err := s.Set("/a/b/", ip(17)); err != nil {
		t.Fatal(err)
	}
	if c, ok := s.Get("/a/b"); !ok || c != 17 {
		t.Fatalf("Get = %d, %v", c, ok)
	}
	if c, ok := Open(path, nil).Get("/a/b"); !ok || c != 17 {
		t.Fatalf("reopened Get = %d, %v", c, ok)
	}
}

func TestAuto(t *testing.T) {
	s := Open(filepath.Join(t.TempDir(), "h.json"), nil)
	s.Set("/a", ip(3))
	if err := s.Set("/a", nil); err != nil {
		t.Fatal(err)
	}
	if _, ok := s.Get("/a"); ok {
		t.Fatal("still set")
	}
}

func TestNone(t *testing.T) {
	s := Open(filepath.Join(t.TempDir(), "h.json"), nil)
	s.Set("/a", ip(-1))
	if c, ok := s.Get("/a"); !ok || c != -1 {
		t.Fatalf("Get = %d, %v", c, ok)
	}
	if err := s.Set("/a", ip(104)); err == nil {
		t.Fatal("104 accepted")
	}
}

func TestCorruptFile(t *testing.T) {
	path := filepath.Join(t.TempDir(), "h.json")
	os.WriteFile(path, []byte("{not json"), 0o600)
	var warns []string
	s := Open(path, func(m string) { warns = append(warns, m) })
	if len(warns) != 1 {
		t.Fatalf("warnings: %v", warns)
	}
	if _, ok := s.Get("/a"); ok {
		t.Fatal("corrupt file gave a hat")
	}
	if err := s.Set("/a", ip(5)); err != nil {
		t.Fatal(err)
	}
	if c, ok := Open(path, func(m string) { t.Fatal(m) }).Get("/a"); !ok || c != 5 {
		t.Fatalf("rewritten file: %d, %v", c, ok)
	}
}

func TestAtomicWrite(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "h.json")
	s := Open(path, nil)
	for i := range 3 {
		if err := s.Set("/a", ip(i)); err != nil {
			t.Fatal(err)
		}
	}
	ents, _ := os.ReadDir(dir)
	if len(ents) != 1 || ents[0].Name() != "h.json" {
		t.Fatalf("leftover files: %v", ents)
	}
	b, _ := os.ReadFile(path)
	if !strings.HasPrefix(string(b), "{") {
		t.Fatalf("file: %q", b)
	}
}

func TestDefaultPathEnv(t *testing.T) {
	t.Setenv("CUBE_HATS_FILE", "/x/y.json")
	if DefaultPath() != "/x/y.json" {
		t.Fatal(DefaultPath())
	}
}
