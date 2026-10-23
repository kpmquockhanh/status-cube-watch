#!/usr/bin/env bash
# Smoke test for the nav server. Env: NAV_KEY (required), NAV_URL (default https://nav.devxdev.site).
set -u
URL="${NAV_URL:-https://nav.devxdev.site}"
KEY="${NAV_KEY:-}"
TILE=/tiles/15/26095/15397.json
fail() { echo "smoke: FAILED: $1" >&2; exit 1; }

code() { curl -s -o /dev/null -w '%{http_code}' --max-time 30 "$@"; }


[ "$(code -X POST "$URL/route" -H 'content-type: application/json' -d '{}')" = 401 ] || fail "no key on /route is not 401"
[ "$(code "$URL$TILE")" = 401 ] || fail "no key on tile is not 401"
[ "$(code -X POST "$URL/route" -H 'X-Nav-Key: wrong' -H 'content-type: application/json' -d '{}')" = 401 ] || fail "wrong key on /route is not 401"
[ "$(code "$URL$TILE" -H 'X-Nav-Key: wrong')" = 401 ] || fail "wrong key on tile is not 401"

route() { # name lon1 lat1 lon2 lat2
  local out
  out=$(mktemp)
  local c
  c=$(curl -s -o "$out" -w '%{http_code}' --max-time 60 -X POST "$URL/route" \
    -H "X-Nav-Key: $KEY" -H 'content-type: application/json' \
    -d "{\"points\":[[$2,$3],[$4,$5]],\"profile\":\"moto_vn\",\"instructions\":true,\"points_encoded\":false,\"locale\":\"en\",\"details\":[\"road_class\"]}")
  [ "$c" = 200 ] || { rm -f "$out"; fail "$1 route returned $c"; }
  node -e '
    const p=JSON.parse(require("fs").readFileSync(process.argv[1],"utf8")).paths[0];
    if(!(p.instructions.length>1)) { console.error("too few instructions"); process.exit(1); }
    if(p.details.road_class.some(d=>String(d[2]).toLowerCase()==="motorway")) { console.error("MOTORWAY in route"); process.exit(1); }
  ' "$out" || { rm -f "$out"; fail "$1 route content"; }
  [ -n "${SAVE:-}" ] && [ "$1" = hcmc ] && cp "$out" "$SAVE"
  rm -f "$out"
}
route hcmc 106.6980 10.7725 106.6588 10.8185
route hanoi 105.8523 21.0287 105.8042 21.2187

t=$(mktemp)
[ "$(curl -s -o "$t" -w '%{http_code}' --max-time 30 -H "X-Nav-Key: $KEY" "$URL$TILE")" = 200 ] || { rm -f "$t"; fail "tile not 200"; }
node -e 'const j=JSON.parse(require("fs").readFileSync(process.argv[1],"utf8")); if(!j.w||!j.w.length) process.exit(1)' "$t" || { rm -f "$t"; fail "tile has empty w"; }
rm -f "$t"
[ "$(code -H "X-Nav-Key: $KEY" "$URL/health")" = 200 ] || fail "/health not 200"

echo "smoke: all checks passed"
