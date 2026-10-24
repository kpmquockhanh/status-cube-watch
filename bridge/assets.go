// Package bridge holds assets embedded into the cube-bridge binary.
package bridge

import _ "embed"

// PreviewHTML is the browser mock served at GET /.
//
//go:embed preview.html
var PreviewHTML []byte
