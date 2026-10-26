#!/bin/bash
# Builds "build/Claude Cube.saver": the CubeSaver dylib (SwiftPM) in a screen saver bundle whose Info.plist
# names its principal class and the bridge's port (CubePort, from CUBE_PORT, default 8787), signed like the app.
set -euo pipefail
cd "$(dirname "$0")"

swift build -c release --product CubeSaver
SAVER="build/Claude Cube.saver"
rm -rf "$SAVER"
mkdir -p "$SAVER/Contents/MacOS" "$SAVER/Contents/Resources"
cp .build/release/libCubeSaver.dylib "$SAVER/Contents/MacOS/CubeSaver"
cp icon/AppIcon.icns "$SAVER/Contents/Resources/AppIcon.icns"
cat > "$SAVER/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleIdentifier</key><string>com.claude-cube.saver</string>
  <key>CFBundleName</key><string>Claude Cube</string>
  <key>CFBundleDisplayName</key><string>Claude Cube</string>
  <key>CFBundleExecutable</key><string>CubeSaver</string>
  <key>CFBundlePackageType</key><string>BNDL</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>NSPrincipalClass</key><string>CubeSaverView</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>CubePort</key><integer>${CUBE_PORT:-8787}</integer>
</dict>
</plist>
PLIST
IDENTITY="${CUBE_SIGN_IDENTITY:-$(security find-identity -v -p codesigning 2>/dev/null |
  awk '/"(Apple Development|Developer ID Application):/ { print $2; exit }')}"
codesign --force --sign "${IDENTITY:--}" --identifier com.claude-cube.saver "$SAVER"
echo "built $(pwd)/$SAVER"
