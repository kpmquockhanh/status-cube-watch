#!/bin/bash
# Claude Cube Link as a macOS LaunchAgent, like bridge/agent.sh.
#   ./install.sh install | status | logs | restart | uninstall
# The app supervises the Node bridge, so this replaces `bridge/agent.sh install`
# for the Bluetooth setup (an already-running bridge is adopted, not duplicated).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
LABEL="com.claude-cube.link"
APP_DST="$HOME/Applications/ClaudeCubeLink.app"
APP_BIN="$APP_DST/Contents/MacOS/ClaudeCubeLink"
BUNDLE_ID="com.claude-cube.link"  # Info.plist CFBundleIdentifier, i.e. the app's defaults domain
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/claude-cube-link.log"
DOMAIN="gui/$(id -u)"

# Writes the LaunchAgent plist to $1 (split out so it can be checked without installing).
write_plist() {
  local node_found port
  node_found="$(command -v node || true)"
  port="${CUBE_PORT:-8787}"
  cat > "$1" <<PLIST_EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key><array><string>$APP_BIN</string></array>
  <key>EnvironmentVariables</key><dict>
    <key>CUBE_BRIDGE_DIR</key><string>$REPO/bridge</string>
    <key>CUBE_PORT</key><string>$port</string>
    <key>CUBE_NODE</key><string>$node_found</string>
  </dict>
  <key>RunAtLoad</key><true/>
  <!-- Restart on a crash only. The app exits 0 on SIGTERM and when a second
       instance finds the lock held; a plain KeepAlive would respawn-loop on those. -->
  <key>KeepAlive</key><dict><key>SuccessfulExit</key><false/></dict>
  <key>ThrottleInterval</key><integer>10</integer>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
</dict></plist>
PLIST_EOF
}

# Stops any running instance (the LaunchAgent's, or one opened by hand). It holds the single-instance
# lock, so a new copy started while it runs exits at once, before macOS can ask for Bluetooth.
stop_app() {
  launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
  pkill -f "$APP_BIN" 2>/dev/null || true
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    pgrep -f "$APP_BIN" >/dev/null || return 0
    sleep 0.5
  done
  echo "warning: Claude Cube Link is still running; quit it from its menu bar item" >&2
}

# Records the bridge and node paths in the app's defaults, for launches without the LaunchAgent's
# environment (Finder, `open`: no CUBE_* variables, working directory "/").
record_paths() {
  local node_found
  node_found="$(command -v node || true)"
  defaults write "$BUNDLE_ID" bridgeDir -string "$REPO/bridge"
  if [ -n "$node_found" ]; then
    defaults write "$BUNDLE_ID" nodePath -string "$node_found"
  else
    defaults delete "$BUNDLE_ID" nodePath 2>/dev/null || true
  fi
}

case "${1:-}" in
  install)
    "$HERE/build-app.sh"
    stop_app  # a reinstall: the old instance must be gone before the `open -a` below
    mkdir -p "$HOME/Applications" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
    rm -rf "$APP_DST"
    cp -R "$HERE/build/ClaudeCubeLink.app" "$APP_DST"
    write_plist "$PLIST"
    record_paths

    # macOS shows the Bluetooth permission prompt the first time the app runs
    # from a normal launch, so start it once by hand, then hand over to launchd.
    echo "Starting Claude Cube Link once so macOS can ask for Bluetooth permission."
    echo "Click Allow, then come back here."
    open -a "$APP_DST"
    read -r -p "Press Enter after you have allowed Bluetooth... " _
    stop_app
    launchctl bootstrap "$DOMAIN" "$PLIST"
    echo "Installed. Logs: $0 logs"
    ;;
  status)
    launchctl print "$DOMAIN/$LABEL" 2>/dev/null | grep -E "state|pid|last exit" || echo "not loaded"
    ;;
  logs)
    tail -n 50 -f "$LOG"
    ;;
  restart)
    launchctl kickstart -k "$DOMAIN/$LABEL"
    ;;
  uninstall)
    stop_app
    rm -f "$PLIST"
    rm -rf "$APP_DST"
    defaults delete "$BUNDLE_ID" bridgeDir 2>/dev/null || true
    defaults delete "$BUNDLE_ID" nodePath 2>/dev/null || true
    echo "Uninstalled. (Bluetooth permission and the cube's pairing are kept.)"
    ;;
  *)
    echo "usage: $0 install|status|logs|restart|uninstall" >&2
    exit 2
    ;;
esac
