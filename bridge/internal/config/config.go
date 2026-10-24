// Package config loads the bridge settings from env vars and config.json,
// with the same precedence and leniency as the Node bridge.
package config

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"strconv"
	"strings"
)

// Config is the resolved bridge configuration.
type Config struct {
	Source        string // "local" | "admin"
	Port          int
	Host          string // "" = every interface
	RefreshMs     int
	AdminKey      string
	OAuthToken    string
	ExtraCards    bool
	GmailUser     string
	GmailPassword string
}

// ReadFile returns the file's bytes, or nil on any error.
func ReadFile(path string) []byte {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil
	}
	return b
}

// Load resolves the config. lookup is os.LookupEnv in production; file is
// config.json's bytes (nil = absent or unreadable). A bad value falls back to
// the default and is reported through warn.
func Load(lookup func(string) (string, bool), file []byte, warn func(string)) Config {
	var f map[string]any
	if json.Unmarshal(file, &f) != nil || f == nil {
		f = map[string]any{}
	}

	// An env var set to "" counts as unset, so it does not mask config.json.
	env := func(name string) any {
		if v, ok := lookup(name); ok && v != "" {
			return v
		}
		return nil
	}
	// Secrets use nullish semantics: set at all (even empty) wins.
	secret := func(name string, fileVal any) string {
		if v, ok := lookup(name); ok {
			return v
		}
		return str(fileVal)
	}
	first := func(vals ...any) any {
		for _, v := range vals {
			if v != nil {
				return v
			}
		}
		return nil
	}

	gmail, _ := f["gmail"].(map[string]any)
	source := str(first(env("CUBE_SOURCE"), f["source"]))
	if source == "" {
		source = "local"
	}
	defRefresh := 5000
	if source == "admin" {
		defRefresh = 60000
	}

	c := Config{
		Source:        source,
		Port:          int(num(warn, "port", first(env("CUBE_PORT"), f["port"]), 8787, 1, 65535, true)),
		Host:          str(first(env("CUBE_HOST"), f["host"])),
		RefreshMs:     int(num(warn, "refreshMs", first(env("CUBE_REFRESH_MS"), f["refreshMs"]), float64(defRefresh), 1000, 86400000, false)),
		AdminKey:      secret("ANTHROPIC_ADMIN_KEY", f["adminKey"]),
		OAuthToken:    secret("ANTHROPIC_ADMIN_OAUTH_TOKEN", f["oauthToken"]),
		ExtraCards:    boolean(warn, "extraCards", env("CUBE_EXTRA_CARDS"), f["extraCards"]),
		GmailUser:     secret("CUBE_GMAIL_USER", gmail["user"]),
		GmailPassword: secret("CUBE_GMAIL_PASSWORD", gmail["appPassword"]),
	}
	// The Usage & Cost API documents sustained polling at once per minute.
	if c.Source == "admin" && c.RefreshMs < 60000 {
		warn("admin source: raising refresh interval to the documented 60s minimum")
		c.RefreshMs = 60000
	}
	return c
}

func str(v any) string {
	s, _ := v.(string)
	return s
}

// num accepts a JSON number or a numeric string; anything else warns and
// yields def. nil or "" means unset.
func num(warn func(string), name string, raw any, def, min, max float64, integer bool) float64 {
	if raw == nil || raw == "" {
		return def
	}
	var n float64
	ok := false
	switch v := raw.(type) {
	case float64:
		n, ok = v, true
	case string:
		var err error
		n, err = strconv.ParseFloat(strings.TrimSpace(v), 64)
		ok = err == nil
	}
	if ok && !math.IsNaN(n) && !math.IsInf(n, 0) && n >= min && n <= max && (!integer || n == math.Trunc(n)) {
		return n
	}
	kind := "a number"
	if integer {
		kind = "an integer"
	}
	j, _ := json.Marshal(raw)
	warn(fmt.Sprintf("config: ignoring %s=%s (want %s %s..%s), using %s", name, j, kind,
		strconv.FormatFloat(min, 'f', -1, 64), strconv.FormatFloat(max, 'f', -1, 64),
		strconv.FormatFloat(def, 'f', -1, 64)))
	return def
}

// boolean takes the first non-empty of raws (env first, then file).
func boolean(warn func(string), name string, raws ...any) bool {
	for _, raw := range raws {
		if raw == nil || raw == "" {
			continue
		}
		if b, ok := raw.(bool); ok {
			return b
		}
		s := strings.ToLower(strings.TrimSpace(fmt.Sprint(raw)))
		switch s {
		case "1", "true", "yes", "on":
			return true
		case "0", "false", "no", "off":
			return false
		}
		j, _ := json.Marshal(raw)
		warn(fmt.Sprintf("config: ignoring %s=%s (want 1/0 or true/false)", name, j))
	}
	return false
}
