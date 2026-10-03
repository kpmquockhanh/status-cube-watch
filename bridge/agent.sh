#!/usr/bin/env bash
# Runs the bridge as a macOS LaunchAgent so the cube keeps working without a
# terminal open: it starts at login and launchd restarts it if it dies.
#
#   ./agent.sh install     write the plist and start it
#   ./agent.sh uninstall   stop it and remove the plist
#   ./agent.sh status      is it loaded, is it answering
#   ./agent.sh logs        tail the log
#   ./agent.sh restart     reload after editing cards.mjs
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
# The config path goes in as an argument, not spliced into the script, so a
# directory name with a quote in it cannot break (or inject into) the JS.
read_config() { # key default
  node -e 'try{const v=JSON.parse(require("fs").readFileSync(process.argv[1],"utf8"))[process.argv[2]];process.stdout.write(String(v||process.argv[3]))}catch{process.stdout.write(process.argv[3])}' \
    "$DIR/config.json" "$1" "$2" 2>/dev/null || echo "$2"
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

# launchd does not read your shell profile, so the interpreter has to be an
# absolute path that will still exist after an nvm version is cleaned up.
pick_node() {
  if [ -n "${CUBE_NODE:-}" ]; then echo "$CUBE_NODE"; return; fi
  for n in /opt/homebrew/bin/node /usr/local/bin/node "$(command -v node || true)"; do
    [ -x "$n" ] && { echo "$n"; return; }
  done
  echo "no node found; set CUBE_NODE=/path/to/node" >&2
  exit 1
}

install_agent() {
  local node; node="$(pick_node)"
  echo "node    $node ($("$node" --version))"
  echo "bridge  $DIR/server.mjs"
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

  local x_node x_dir x_log
  x_node="$(xml_escape "$node")"; x_dir="$(xml_escape "$DIR")"; x_log="$(xml_escape "$LOG_DIR")"

  cat > "$PLIST" <<PLIST_EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$x_node</string>
    <string>$x_dir/server.mjs</string>
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
    curl -fsS "http://$URL_HOST:$PORT/api/status" |
      node -e "let s='';process.stdin.on('data',d=>s+=d).on('end',()=>{for(const c of JSON.parse(s).cards)console.log('    '+[c.t,c.v,c.s1,c.s2].join(' | '))})"
  else
    echo "  not answering on port $PORT -- see $LOG_DIR/bridge.log"
  fi
}

case "${1:-status}" in
  install)   install_agent ;;
  uninstall) uninstall_agent ;;
  restart)   launchctl kickstart -k "$TARGET"; sleep 2; status_agent ;;
  status)    status_agent ;;
  logs)      tail -f "$LOG_DIR/bridge.log" ;;
  *) sed -n '2,12p' "${BASH_SOURCE[0]}"; exit 1 ;;
esac
