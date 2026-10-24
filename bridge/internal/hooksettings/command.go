// Package hooksettings builds and edits the bridge's entries in Claude Code's settings.json.
package hooksettings

import (
	"fmt"
	"strings"
)

// Command is the hook command line written into settings.json: it runs the bridge's
// `hook` subcommand when the binary exists and always exits 0.
func Command(binary string, port int) string {
	q := "'" + strings.ReplaceAll(binary, "'", `'\''`) + "'"
	return fmt.Sprintf("[ -x %s ] && %s hook --port %d >/dev/null 2>&1; exit 0", q, q, port)
}
