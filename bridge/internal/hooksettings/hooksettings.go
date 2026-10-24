package hooksettings

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
)

// Events are the Claude Code hook events the bridge listens to.
var Events = []string{"SessionStart", "UserPromptSubmit", "PreToolUse", "PostToolUse", "PermissionRequest", "Notification", "Stop", "SessionEnd", "PreCompact"}

// matcherEvents get `"matcher": "*"` on their group, as in agents-deck's HooksSettings.
var matcherEvents = map[string]bool{"PreToolUse": true, "PostToolUse": true, "PermissionRequest": true}

// IsOurs reports whether a hook command is one Command wrote. It matches only
// that shape, so a user's own hooks are never touched.
func IsOurs(command string) bool {
	return strings.Contains(command, "cube-bridge") && strings.Contains(command, "' hook --port ")
}

// Install adds one group running command to every event in Events, after the
// user's own groups, replacing any entries an earlier install left. Nothing
// else in the file changes, key order included. A missing file is created.
func Install(path, command string) error {
	return edit(path, true, func(top object) (object, bool, error) {
		return apply(top, command)
	})
}

// Uninstall removes the bridge's hook entries, and any group, event or hooks
// object they leave empty. A missing file, or one without our entries, is left alone.
func Uninstall(path string) error {
	return edit(path, false, func(top object) (object, bool, error) {
		return apply(top, "")
	})
}

// apply strips our hooks from top and, when command is set, appends our group
// to every event. It reports whether anything changed.
func apply(top object, command string) (object, bool, error) {
	i := top.index("hooks")
	var hooks object
	if i >= 0 {
		var err error
		if hooks, err = parseObject(top[i].Value); err != nil {
			return nil, false, fmt.Errorf("\"hooks\": %w", err)
		}
	}
	changed := false
	var out object
	for _, ev := range hooks {
		groups, err := parseArray(ev.Value)
		if err != nil {
			return nil, false, fmt.Errorf("\"hooks\".%q: %w", ev.Key, err)
		}
		kept, removed := stripGroups(groups)
		if removed && len(kept) == 0 {
			changed = true
			continue // an event only we used
		}
		if removed {
			changed = true
			ev.Value = encodeArray(kept)
		}
		out = append(out, ev)
	}
	if command != "" {
		for _, name := range Events {
			g := ourGroup(name, command)
			if j := out.index(name); j >= 0 {
				groups, _ := parseArray(out[j].Value)
				out[j].Value = encodeArray(append(groups, g))
			} else {
				out = append(out, field{name, encodeArray([]json.RawMessage{g})})
			}
		}
		changed = true
	}
	switch {
	case !changed:
		return top, false, nil
	case len(out) == 0 && i >= 0:
		return append(top[:i:i], top[i+1:]...), true, nil
	case i >= 0:
		top[i].Value = encodeObject(out)
	default:
		top = append(top, field{"hooks", encodeObject(out)})
	}
	return top, true, nil
}

// stripGroups removes our hooks from each group, dropping groups left with none.
// Groups without our hooks, or not shaped as expected, keep their raw bytes.
func stripGroups(groups []json.RawMessage) (kept []json.RawMessage, removed bool) {
	for _, g := range groups {
		obj, err := parseObject(g)
		j := -1
		if err == nil {
			j = obj.index("hooks")
		}
		if j < 0 {
			kept = append(kept, g)
			continue
		}
		hs, err := parseArray(obj[j].Value)
		if err != nil {
			kept = append(kept, g)
			continue
		}
		var left []json.RawMessage
		for _, h := range hs {
			var cmd struct {
				Command string `json:"command"`
			}
			if json.Unmarshal(h, &cmd) == nil && IsOurs(cmd.Command) {
				continue
			}
			left = append(left, h)
		}
		switch {
		case len(left) == len(hs):
			kept = append(kept, g)
		case len(left) > 0:
			removed = true
			obj[j].Value = encodeArray(left)
			kept = append(kept, encodeObject(obj))
		default:
			removed = true
		}
	}
	return kept, removed
}

func ourGroup(event, command string) json.RawMessage {
	hook := object{{"type", marshal("command")}, {"command", marshal(command)}, {"timeout", json.RawMessage("5")}}
	var g object
	if matcherEvents[event] {
		g = append(g, field{"matcher", marshal("*")})
	}
	g = append(g, field{"hooks", encodeArray([]json.RawMessage{encodeObject(hook)})})
	return encodeObject(g)
}

// edit reads path, runs fn on its top-level object and, when fn changed it,
// writes the result atomically. create allows a missing file (as {}).
func edit(path string, create bool, fn func(object) (object, bool, error)) error {
	target, err := filepath.EvalSymlinks(path)
	missing := errors.Is(err, fs.ErrNotExist)
	switch {
	case missing && !create:
		return nil
	case missing:
		target = path // a dangling symlink is refused by the write below
	case err != nil:
		return fmt.Errorf("%s: %w", path, err)
	}
	var orig []byte
	mode := fs.FileMode(0o600)
	top := object{}
	if !missing {
		st, err := os.Stat(target)
		if err != nil {
			return fmt.Errorf("%s: %w", path, err)
		}
		mode = st.Mode().Perm()
		if orig, err = os.ReadFile(target); err != nil {
			return fmt.Errorf("%s: %w", path, err)
		}
		if top, err = parseObject(orig); err != nil {
			return fmt.Errorf("%s: not a JSON object, left unchanged: %w", path, err)
		}
	}
	top, changed, err := fn(top)
	if err != nil {
		return fmt.Errorf("%s: %w, left unchanged", path, err)
	}
	if !changed {
		return nil
	}
	var out bytes.Buffer
	if err := json.Indent(&out, encodeObject(top), "", "  "); err != nil {
		return fmt.Errorf("%s: %w", path, err)
	}
	out.WriteByte('\n')
	if !missing {
		if err := backup(path+".cube-bridge.bak", orig, mode); err != nil {
			return err
		}
	} else if err := os.MkdirAll(filepath.Dir(target), 0o700); err != nil {
		return err
	}
	return writeAtomic(target, out.Bytes(), mode, missing)
}

// backup writes the original bytes once; an existing backup is never replaced.
func backup(path string, b []byte, mode fs.FileMode) error {
	f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, mode)
	if errors.Is(err, fs.ErrExist) {
		return nil
	}
	if err != nil {
		return fmt.Errorf("backup %s: %w", path, err)
	}
	_, err = f.Write(b)
	if err == nil {
		err = f.Sync()
	}
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		os.Remove(path)
		return fmt.Errorf("backup %s: %w", path, err)
	}
	return nil
}

// writeAtomic replaces target through a synced temp file in its own directory.
func writeAtomic(target string, b []byte, mode fs.FileMode, create bool) (err error) {
	if create {
		if _, err := os.Lstat(target); err == nil {
			return fmt.Errorf("%s: dangling symlink, left unchanged", target)
		}
	}
	dir := filepath.Dir(target)
	f, err := os.CreateTemp(dir, "."+filepath.Base(target)+".cube-bridge-*")
	if err != nil {
		return err
	}
	defer func() {
		if err != nil {
			f.Close()
			os.Remove(f.Name())
		}
	}()
	if err = f.Chmod(mode); err != nil {
		return err
	}
	if _, err = f.Write(b); err != nil {
		return err
	}
	if err = f.Sync(); err != nil {
		return err
	}
	if err = f.Close(); err != nil {
		return err
	}
	if err = os.Rename(f.Name(), target); err != nil {
		return err
	}
	if d, derr := os.Open(dir); derr == nil { // best effort: persist the rename
		d.Sync()
		d.Close()
	}
	return nil
}

// field is one member of a JSON object; Value keeps the member's raw bytes.
type field struct {
	Key   string
	Value json.RawMessage
}

// object is a JSON object that keeps its key order.
type object []field

func (o object) index(key string) int {
	for i, f := range o {
		if f.Key == key {
			return i
		}
	}
	return -1
}

// parseObject decodes one JSON object (and nothing after it) keeping key order
// and each value's raw bytes.
func parseObject(b []byte) (object, error) {
	dec := json.NewDecoder(bytes.NewReader(b))
	if err := expectDelim(dec, '{'); err != nil {
		return nil, err
	}
	o := object{}
	for dec.More() {
		tok, err := dec.Token()
		if err != nil {
			return nil, err
		}
		key, _ := tok.(string)
		var v json.RawMessage
		if err := dec.Decode(&v); err != nil {
			return nil, err
		}
		o = append(o, field{key, v})
	}
	if err := expectDelim(dec, '}'); err != nil {
		return nil, err
	}
	return o, expectEOF(dec)
}

// parseArray decodes one JSON array keeping each element's raw bytes.
func parseArray(b []byte) ([]json.RawMessage, error) {
	dec := json.NewDecoder(bytes.NewReader(b))
	if err := expectDelim(dec, '['); err != nil {
		return nil, err
	}
	var a []json.RawMessage
	for dec.More() {
		var v json.RawMessage
		if err := dec.Decode(&v); err != nil {
			return nil, err
		}
		a = append(a, v)
	}
	if err := expectDelim(dec, ']'); err != nil {
		return nil, err
	}
	return a, expectEOF(dec)
}

func expectDelim(dec *json.Decoder, want json.Delim) error {
	tok, err := dec.Token()
	if err != nil {
		if errors.Is(err, io.EOF) {
			return io.ErrUnexpectedEOF
		}
		return err
	}
	if d, ok := tok.(json.Delim); !ok || d != want {
		kind := "an object"
		if want == '[' || want == ']' {
			kind = "an array"
		}
		return fmt.Errorf("expected %s, found %v", kind, tok)
	}
	return nil
}

func expectEOF(dec *json.Decoder) error {
	if _, err := dec.Token(); !errors.Is(err, io.EOF) {
		return errors.New("unexpected data after the JSON value")
	}
	return nil
}

func encodeObject(o object) json.RawMessage {
	var b bytes.Buffer
	b.WriteByte('{')
	for i, f := range o {
		if i > 0 {
			b.WriteByte(',')
		}
		b.Write(marshal(f.Key))
		b.WriteByte(':')
		b.Write(f.Value)
	}
	b.WriteByte('}')
	return b.Bytes()
}

func encodeArray(a []json.RawMessage) json.RawMessage {
	var b bytes.Buffer
	b.WriteByte('[')
	for i, v := range a {
		if i > 0 {
			b.WriteByte(',')
		}
		b.Write(v)
	}
	b.WriteByte(']')
	return b.Bytes()
}

// marshal encodes a string without HTML escaping, so `&&` and `>` stay readable.
func marshal(s string) json.RawMessage {
	var b bytes.Buffer
	enc := json.NewEncoder(&b)
	enc.SetEscapeHTML(false)
	_ = enc.Encode(s)
	return bytes.TrimRight(b.Bytes(), "\n")
}
