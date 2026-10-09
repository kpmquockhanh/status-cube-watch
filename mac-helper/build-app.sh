#!/bin/bash
# Builds build/ClaudeCubeLink.app: the SwiftPM executable in a bundle with the
# Info.plist that carries the Bluetooth usage description and a copy of the Node
# bridge (Contents/Resources/bridge, run with the system's node), ad-hoc signed.
set -euo pipefail
cd "$(dirname "$0")"

swift build -c release
APP=build/ClaudeCubeLink.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cp .build/release/ClaudeCubeLink "$APP/Contents/MacOS/ClaudeCubeLink"
cp Info.plist "$APP/Contents/Info.plist"
# Regenerate the icon with: swift icon/make-icon.swift X.iconset && iconutil -c icns X.iconset -o icon/AppIcon.icns
mkdir -p "$APP/Contents/Resources"
cp icon/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
# The bridge's code only: config.json (gitignored, holds secrets) stays out of the bundle. The app
# keeps the bridge settings itself and passes them to the child as environment variables.
BRIDGE=../bridge
mkdir -p "$APP/Contents/Resources/bridge"
(cd "$BRIDGE" && find . -name '*.mjs' -not -path './node_modules/*' -print0) |
  while IFS= read -r -d '' f; do
    mkdir -p "$APP/Contents/Resources/bridge/$(dirname "$f")"
    cp "$BRIDGE/$f" "$APP/Contents/Resources/bridge/$f"
  done
cp "$BRIDGE/package.json" "$BRIDGE/preview.html" "$BRIDGE/config.example.json" "$APP/Contents/Resources/bridge/"
codesign --force --sign - --identifier com.claude-cube.link "$APP"
echo "built $(pwd)/$APP"
