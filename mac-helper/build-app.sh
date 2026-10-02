#!/bin/bash
# Builds build/ClaudeCubeLink.app: the SwiftPM executable in a bundle with the
# Info.plist that carries the Bluetooth usage description, ad-hoc signed.
set -euo pipefail
cd "$(dirname "$0")"

swift build -c release
APP=build/ClaudeCubeLink.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cp .build/release/ClaudeCubeLink "$APP/Contents/MacOS/ClaudeCubeLink"
cp Info.plist "$APP/Contents/Info.plist"
codesign --force --sign - --identifier com.claude-cube.link "$APP"
echo "built $(pwd)/$APP"
