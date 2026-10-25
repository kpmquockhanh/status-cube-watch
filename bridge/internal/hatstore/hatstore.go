// Package hatstore keeps the per-session hat overrides the cube's hat picker
// sets: a JSON object {"<key>": code} (keys are cards.HatKey: "session:<id>") in the user's config directory.
package hatstore

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sync"
)

// MaxCode is the highest hat code (shape x 8 + colour, 13 shapes, 8 colours).
const MaxCode = 103

// DefaultPath is $CUBE_HATS_FILE, else <user config dir>/claude-status-cube/hats.json.
func DefaultPath() string {
	if p := os.Getenv("CUBE_HATS_FILE"); p != "" {
		return p
	}
	dir, err := os.UserConfigDir()
	if err != nil {
		dir = "."
	}
	return filepath.Join(dir, "claude-status-cube", "hats.json")
}

// Store is safe for concurrent use.
type Store struct {
	path string
	mu   sync.Mutex
	m    map[string]int
}

// Open reads path. A missing file is an empty store; an unreadable or corrupt
// one is reported once through warn and treated as empty (the next Set
// rewrites it).
func Open(path string, warn func(string)) *Store {
	s := &Store{path: path, m: map[string]int{}}
	b, err := os.ReadFile(path)
	if err != nil {
		if !errors.Is(err, fs.ErrNotExist) && warn != nil {
			warn(fmt.Sprintf("hats: cannot read %s: %v", path, err))
		}
		return s
	}
	var m map[string]int
	if err := json.Unmarshal(b, &m); err != nil {
		if warn != nil {
			warn(fmt.Sprintf("hats: ignoring %s: %v", path, err))
		}
		return s
	}
	for k, v := range m {
		if v >= -1 && v <= MaxCode {
			s.m[filepath.Clean(k)] = v
		}
	}
	return s
}

// Get returns the override for key: code 0..MaxCode, or -1 = no hat;
// ok=false = automatic.
func (s *Store) Get(key string) (code int, ok bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	code, ok = s.m[filepath.Clean(key)]
	return code, ok
}

// Set stores code for key; nil = automatic (the override is removed). The
// file is replaced atomically; on a write error the store is unchanged.
func (s *Store) Set(k string, code *int) error {
	if code != nil && (*code < -1 || *code > MaxCode) {
		return fmt.Errorf("hat %d out of range", *code)
	}
	key := filepath.Clean(k)
	s.mu.Lock()
	defer s.mu.Unlock()
	next := make(map[string]int, len(s.m)+1)
	for k, v := range s.m {
		next[k] = v
	}
	if code == nil {
		delete(next, key)
	} else {
		next[key] = *code
	}
	if err := write(s.path, next); err != nil {
		return err
	}
	s.m = next
	return nil
}

func write(path string, m map[string]int) error {
	b, err := json.MarshalIndent(m, "", "  ")
	if err != nil {
		return err
	}
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return err
	}
	f, err := os.CreateTemp(dir, ".hats-*.tmp")
	if err != nil {
		return err
	}
	tmp := f.Name()
	_, err = f.Write(append(b, '\n'))
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Rename(tmp, path)
	}
	if err != nil {
		os.Remove(tmp)
	}
	return err
}
