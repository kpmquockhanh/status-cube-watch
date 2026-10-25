package config

import "testing"

func TestAgentsFlag(t *testing.T) {
	c, _ := load(nil, "")
	if !c.Agents {
		t.Fatal("default should be true")
	}
	c, _ = load(nil, `{"agents": false}`)
	if c.Agents {
		t.Fatal("file false should disable")
	}
	c, _ = load(map[string]string{"CUBE_AGENTS": "0"}, "")
	if c.Agents {
		t.Fatal("env 0 should disable")
	}
	c, _ = load(nil, `{"extraCards": false}`)
	if c.ExtraCards {
		t.Fatal("extraCards must stay off by default")
	}
	c, _ = load(nil, `{"agents": true}`)
	if !c.Agents {
		t.Fatal("file true should enable")
	}
	c, _ = load(map[string]string{"CUBE_AGENTS": "0"}, `{"agents": true}`)
	if c.Agents {
		t.Fatal("env 0 should beat file true")
	}
	c, _ = load(map[string]string{"CUBE_AGENTS": "on"}, "")
	if !c.Agents {
		t.Fatal("env on should enable")
	}
}
