package agents

import (
	"regexp"
	"testing"
	"time"
)

func TestSetStatus(t *testing.T) {
	t0 := time.Unix(1000, 0)
	t1 := t0.Add(time.Minute)
	t2 := t1.Add(time.Minute)
	e := &entry{}
	e.SetStatus(Running, t0)
	e.Activity = &Activity{Tool: "Bash"}
	e.SetStatus(Waiting, t1)
	if !e.StatusSince.Equal(t1) || e.Activity != nil {
		t.Fatalf("running->waiting: %+v", e.Agent)
	}
	e.Waiting = &Prompt{ID: "p1"}
	e.SetStatus(Waiting, t2)
	if !e.StatusSince.Equal(t1) || e.Waiting == nil {
		t.Fatalf("waiting->waiting must keep stamp and prompt: %+v", e.Agent)
	}
	e.SetStatus(Idle, t2)
	if e.Waiting != nil || !e.StatusSince.Equal(t2) {
		t.Fatalf("leaving waiting: %+v", e.Agent)
	}
	e.ErrorText = "boom"
	e.SetStatus(Running, t2)
	if e.ErrorText != "" {
		t.Fatal("leaving error should clear ErrorText")
	}
}

func TestNextPromptID(t *testing.T) {
	e := &entry{}
	if a, b := e.NextPromptID(), e.NextPromptID(); a != "p1" || b != "p2" {
		t.Fatalf("got %s %s", a, b)
	}
}

func TestAgentID(t *testing.T) {
	a, b := agentID("sess-1"), agentID("sess-2")
	if a != agentID("sess-1") || a == b {
		t.Fatalf("unstable or colliding: %s %s", a, b)
	}
	if !regexp.MustCompile(`^l[a-z0-9]{5}$`).MatchString(a) {
		t.Fatalf("bad id %q", a)
	}
}
