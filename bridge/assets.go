// Package bridge holds assets embedded into the cube-bridge binary.
package bridge

import (
	"embed"
	_ "embed"
)

// PreviewHTML is the browser mock served at GET /.
//
//go:embed preview.html
var PreviewHTML []byte

// Board is the web board served under GET /board/ (board/index.html, ...).
//
//go:embed board
var Board embed.FS
