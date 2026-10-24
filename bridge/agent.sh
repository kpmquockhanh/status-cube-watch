#!/usr/bin/env bash
# Runs the bridge as a macOS LaunchAgent so the cube keeps working without a
# terminal open: it starts at login and launchd restarts it if it dies.
#
#   ./agent.sh install     build the binary, write the plist and start it
#   ./agent.sh uninstall   stop it and remove the plist
#   ./agent.sh status      is it loaded, is it answering
#   ./agent.sh logs        tail the log
#   ./agent.sh restart     rebuild and reload after editing the Go code
#
# A LaunchAgent (not a LaunchDaemon) is the right unit here: it runs as you,
# inside your GUI login session, which is what lets it read the Claude Code
# token out of your login Keychain. A daemon runs before login with no Keychain
# and could not authenticate.

set -euo pipefail

LABEL="com.claude-status-cube.bridge"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG_DIR="$HOME/Library/Logs/claude-status-cube"
TARGET="gui/$(id -u)/$LABEL"
read_config() { # key default
  local v
  v="$(/usr/bin/plutil -extract "$1" raw -o - "$DIR/config.json" 2>/dev/null)" || v=""
  case "$v" in ""|0|false) echo "$2" ;; *) echo "$v" ;; esac
}
PORT="$(read_config port 8787)"
# Where status_agent knocks: the configured host, or loopback when it listens everywhere.
HOST="$(read_config host 127.0.0.1)"
case "$HOST" in 0.0.0.0|::) HOST=127.0.0.1 ;; esac
case "$HOST" in *:*) URL_HOST="[$HOST]" ;; *) URL_HOST="$HOST" ;; esac

# For values spliced into the plist: a path with &, < or > would be invalid XML.
# (sed, not ${s//&/...}: bash 5.2 expands & in a replacement to the match.)
xml_escape() {
  printf '%s' "$1" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' -e 's/"/\&quot;/g' -e "s/'/\&apos;/g"
}

# Writes the LaunchAgent plist to the given path.
write_plist() { # path
  local x_dir x_log
  x_dir="$(xml_escape "$DIR")"; x_log="$(xml_escape "$LOG_DIR")"

  cat > "$1" <<PLIST_EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$x_dir/cube-bridge</string>
  </array>
  <key>WorkingDirectory</key><string>$x_dir</string>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key>
  <dict>
    <!-- Restart on a crash, but not on a clean exit: a deliberate stop
         should stay stopped rather than fight launchd. -->
    <key>SuccessfulExit</key><false/>
  </dict>
  <!-- Back off between respawns so a config mistake cannot spin the CPU. -->
  <key>ThrottleInterval</key><integer>10</integer>
  <key>StandardOutPath</key><string>$x_log/bridge.log</string>
  <key>StandardErrorPath</key><string>$x_log/bridge.log</string>
  <key>ProcessType</key><string>Background</string>
</dict>
</plist>
PLIST_EOF
}

build_bridge() {
  command -v go >/dev/null 2>&1 || { echo "go not found: install Go 1.24+" >&2; exit 1; }
  (cd "$DIR" && go build -o "$DIR/cube-bridge" ./cmd/cube-bridge)
}

install_agent() {
  build_bridge
  echo "bridge  $DIR/cube-bridge"
  echo "logs    $LOG_DIR/bridge.log"
  mkdir -p "$LOG_DIR" "$(dirname "$PLIST")"

  # Anything already listening on the port would make launchd respawn-loop.
  # Only the listener: a bare tcp:PORT match also hits clients that merely have
  # a connection open to it (a browser on the preview, the Mac helper).
  if lsof -ti "tcp:$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
    echo "stopping whatever is already listening on port $PORT"
    lsof -ti "tcp:$PORT" -sTCP:LISTEN | xargs kill 2>/dev/null || true
    sleep 1
  fi

  write_plist "$PLIST"

  launchctl bootout "$TARGET" 2>/dev/null || true
  launchctl bootstrap "gui/$(id -u)" "$PLIST"
  launchctl kickstart -k "$TARGET"
  sleep 2
  status_agent
}

uninstall_agent() {
  launchctl bootout "$TARGET" 2>/dev/null || true
  rm -f "$PLIST"
  echo "removed $PLIST"
}

status_agent() {
  if launchctl print "$TARGET" >/dev/null 2>&1; then
    launchctl print "$TARGET" | awk '/state =|pid =|last exit code/ {$1=$1; print "  " $0}'
  else
    echo "  not loaded"
    return
  fi
  if curl -fsS --max-time 3 "http://$URL_HOST:$PORT/api/status" >/dev/null 2>&1; then
    echo "  serving on port $PORT:"
    echo "    $(curl -fsS "http://$URL_HOST:$PORT/health")"
  else
    echo "  not answering on port $PORT -- see $LOG_DIR/bridge.log"
  fi
}

# What restart should do: not-installed (nothing loaded), install (the plist
# still runs something other than our binary, e.g. an older Node install), or
# kickstart.
restart_action() { # plist loaded(0|1)
  local prog
  if [ "$2" != 1 ]; then echo not-installed; return; fi
  prog="$(/usr/libexec/PlistBuddy -c 'Print :ProgramArguments:0' "$1" 2>/dev/null || true)"
  if [ "$prog" = "$DIR/cube-bridge" ]; then echo kickstart; else echo install; fi
}

restart_agent() {
  local loaded=0
  launchctl print "$TARGET" >/dev/null 2>&1 && loaded=1
  case "$(restart_action "$PLIST" "$loaded")" in
    not-installed) echo "not installed: run ./agent.sh install" >&2; return 1 ;;
    install)       echo "installed plist does not run $DIR/cube-bridge: switching to the Go binary"; install_agent ;;
    kickstart)     build_bridge; launchctl kickstart -k "$TARGET"; sleep 2; status_agent ;;
  esac
}

# Sourced with CUBE_AGENT_LIB=1 (tests, plist preview): define the functions only.
[ -n "${CUBE_AGENT_LIB:-}" ] && return 0 2>/dev/null

case "${1:-status}" in
  install)   install_agent ;;
  uninstall) uninstall_agent ;;
  restart)   restart_agent ;;
  status)    status_agent ;;
  logs)      tail -f "$LOG_DIR/bridge.log" ;;
  *) sed -n '2,12p' "${BASH_SOURCE[0]}"; exit 1 ;;
esac
