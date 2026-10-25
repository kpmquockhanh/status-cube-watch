package config

import (
	"strings"
	"testing"
)

func load(env map[string]string, file string) (Config, []string) {
	var warns []string
	lookup := func(k string) (string, bool) { v, ok := env[k]; return v, ok }
	var f []byte
	if file != "" {
		f = []byte(file)
	}
	return Load(lookup, f, func(s string) { warns = append(warns, s) }), warns
}

func TestDefaults(t *testing.T) {
	c, w := load(nil, "")
	if c != (Config{Source: "local", Port: 8787, RefreshMs: 5000, Agents: true}) || len(w) != 0 {
		t.Fatalf("got %+v %v", c, w)
	}
}

func TestAdminDefaultsAndFloor(t *testing.T) {
	c, w := load(map[string]string{"CUBE_SOURCE": "admin"}, "")
	if c.RefreshMs != 60000 || len(w) != 0 {
		t.Fatalf("got %+v %v", c, w)
	}
	c, w = load(map[string]string{"CUBE_SOURCE": "admin", "CUBE_REFRESH_MS": "5000"}, "")
	if c.RefreshMs != 60000 || len(w) != 1 || !strings.Contains(w[0], "60s") {
		t.Fatalf("got %+v %v", c, w)
	}
}

func TestEnvBeatsFileBothWays(t *testing.T) {
	c, _ := load(map[string]string{"CUBE_EXTRA_CARDS": "0"}, `{"extraCards": true}`)
	if c.ExtraCards {
		t.Fatal("env 0 should beat file true")
	}
	c, _ = load(map[string]string{"CUBE_EXTRA_CARDS": "yes"}, `{"extraCards": false}`)
	if !c.ExtraCards {
		t.Fatal("env yes should beat file false")
	}
}

func TestEmptyEnvIsUnset(t *testing.T) {
	c, _ := load(map[string]string{"CUBE_PORT": "", "CUBE_HOST": ""}, `{"port": 9000, "host": "10.0.0.2"}`)
	if c.Port != 9000 || c.Host != "10.0.0.2" {
		t.Fatalf("got %+v", c)
	}
}

func TestSecretsUseNullish(t *testing.T) {
	c, _ := load(map[string]string{"ANTHROPIC_ADMIN_KEY": ""}, `{"adminKey":"k"}`)
	if c.AdminKey != "" {
		t.Fatalf("set-but-empty env should win, got %q", c.AdminKey)
	}
	c, _ = load(nil, `{"adminKey":"k","oauthToken":"o","gmail":{"user":"u","appPassword":"p"}}`)
	if c.AdminKey != "k" || c.OAuthToken != "o" || c.GmailUser != "u" || c.GmailPassword != "p" {
		t.Fatalf("got %+v", c)
	}
	c, _ = load(map[string]string{"CUBE_GMAIL_USER": "", "CUBE_GMAIL_PASSWORD": "x"}, `{"gmail":{"user":"u","appPassword":"p"}}`)
	if c.GmailUser != "" || c.GmailPassword != "x" {
		t.Fatalf("got %+v", c)
	}
}

func TestBadNumberFallsBack(t *testing.T) {
	c, w := load(map[string]string{"CUBE_PORT": "abc"}, "")
	if c.Port != 8787 || len(w) != 1 || !strings.Contains(w[0], "port") {
		t.Fatalf("got %+v %v", c, w)
	}
	c, _ = load(map[string]string{"CUBE_REFRESH_MS": "500"}, "")
	if c.RefreshMs != 5000 {
		t.Fatalf("got %+v", c)
	}
	c, _ = load(map[string]string{"CUBE_PORT": "70000"}, "")
	if c.Port != 8787 {
		t.Fatalf("got %+v", c)
	}
	c, _ = load(map[string]string{"CUBE_PORT": "80.5"}, "")
	if c.Port != 8787 {
		t.Fatalf("got %+v", c)
	}
}

func TestLoadLooseTypes(t *testing.T) {
	c, w := load(nil, `{"port":"8788","refreshMs":2000.0,"extraCards":"on","host":""}`)
	if c.Port != 8788 || c.RefreshMs != 2000 || !c.ExtraCards || c.Host != "" || len(w) != 0 {
		t.Fatalf("got %+v %v", c, w)
	}
}

func TestBadBoolWarns(t *testing.T) {
	c, w := load(map[string]string{"CUBE_EXTRA_CARDS": "maybe"}, "")
	if c.ExtraCards || len(w) != 1 {
		t.Fatalf("got %+v %v", c, w)
	}
}

func TestBrokenFile(t *testing.T) {
	c, _ := load(nil, `{not json`)
	if c.Port != 8787 || c.Source != "local" {
		t.Fatalf("got %+v", c)
	}
}
