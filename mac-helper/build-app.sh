#!/bin/bash
# Builds build/ClaudeCubeLink.app: the SwiftPM executable in a bundle with the
# Info.plist that carries the Bluetooth usage description and the Go
# bridge binary (Contents/Resources/bridge/cube-bridge), signed.
#
# Signing: CUBE_SIGN_IDENTITY if set ("-" = ad-hoc), else the first Apple Development or Developer ID
# Application identity in the keychain, else ad-hoc. A real identity keeps macOS's grants (Bluetooth,
# Accessibility, Keychain access) across rebuilds; an ad-hoc build is a new program to macOS every time.
set -euo pipefail
cd "$(dirname "$0")"

command -v go >/dev/null 2>&1 || { echo "go not found: install Go 1.24+" >&2; exit 1; }
swift build -c release
APP=build/ClaudeCubeLink.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cp .build/release/ClaudeCubeLink "$APP/Contents/MacOS/ClaudeCubeLink"
cp Info.plist "$APP/Contents/Info.plist"
# Regenerate the icon with: swift icon/make-icon.swift X.iconset && iconutil -c icns X.iconset -o icon/AppIcon.icns
mkdir -p "$APP/Contents/Resources"
cp icon/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
# The Go bridge binary only: config.json (gitignored, holds secrets) stays out of the bundle, and the
# preview page is embedded in the binary. The app keeps the bridge settings itself and passes them to
# the child as environment variables.
BRIDGE="$PWD/../bridge"
APP_ABS="$PWD/$APP"
mkdir -p "$APP/Contents/Resources/bridge"
(cd "$BRIDGE" && go build -trimpath -o "$APP_ABS/Contents/Resources/bridge/cube-bridge" ./cmd/cube-bridge)
IDENTITY="${CUBE_SIGN_IDENTITY:-$(security find-identity -v -p codesigning 2>/dev/null |
  awk '/"(Apple Development|Developer ID Application):/ { print $2; exit }')}"
IDENTITY="${IDENTITY:--}"
codesign --force --sign "$IDENTITY" --identifier com.claude-cube.bridge "$APP/Contents/Resources/bridge/cube-bridge"
codesign --force --sign "$IDENTITY" --identifier com.claude-cube.link "$APP"
if [ "$IDENTITY" = "-" ]; then
  echo "signed ad-hoc: macOS will ask for Accessibility and Keychain access again after each rebuild"
else
  echo "signed with $(codesign -dvv "$APP" 2>&1 | sed -n 's/^Authority=//p' | head -1)"
fi
echo "built $(pwd)/$APP"
