// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, backend/internal/claude/birth_other.go.

//go:build !darwin

package agents

import (
	"os"
	"time"
)

func birthTime(st os.FileInfo) time.Time { return st.ModTime() }
