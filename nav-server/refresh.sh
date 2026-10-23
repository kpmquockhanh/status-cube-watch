#!/usr/bin/env bash
# Rebuild the routing graph and street tiles from a fresh Vietnam extract.
# Safe for cron. Live data is flipped atomically and rolled back if GraphHopper
# does not come up healthy.
#
# Layout under /root/nav/data (mount this whole directory, e.g. at /data):
#   graph -> releases/graph-<stamp>     symlink, relative, flipped with mv -T
#   tiles -> releases/tiles-<stamp>     symlink, relative
#   releases/                           real directories
# GraphHopper must use graph.location=<mount>/graph and nginx must serve
# <mount>/tiles; both resolve the symlinks inside the same mount.
set -euo pipefail

PBF_URL="${PBF_URL:-https://download.geofabrik.de/asia/vietnam-latest.osm.pbf}"
NAV=/root/nav
IMAGE=nav-graphhopper:11.1
COMPOSE="${COMPOSE:-/root/nav-server/docker-compose.yml}"
HEALTH_PATH="${HEALTH_PATH:-/health}"   # tests override this to force a rollback
DC="${DC:-docker compose}"              # tests override this with a wrapper
WORK=$NAV/work
LIVE=$NAV/data
REL=$LIVE/releases

mkdir -p "$NAV" "$WORK" "$LIVE" "$REL"
exec 9>"$NAV/refresh.lock"
flock -n 9 || { echo "[refresh] another refresh is running (lock $NAV/refresh.lock)" >&2; exit 1; }

# point <name> at releases/<target> atomically
flip() { ln -sfn "releases/$2" "$LIVE/.$1.tmp" && mv -T "$LIVE/.$1.tmp" "$LIVE/$1"; }

# --- recover from an interrupted earlier run / migrate the old layout ---------
for n in graph tiles; do
  if [ -d "$LIVE/$n" ] && [ ! -L "$LIVE/$n" ]; then
    mv "$LIVE/$n" "$REL/$n-legacy"
    flip "$n" "$n-legacy"
  fi
  if [ ! -e "$LIVE/$n" ]; then
    last=$(ls -1 "$REL" 2>/dev/null | grep "^$n-" | sort | tail -1 || true)
    if [ -n "$last" ]; then echo "[refresh] restoring $n -> $last"; flip "$n" "$last"; fi
  fi
done

run() { docker run --rm -v "$NAV:$NAV" "$@"; }

rm -rf "$WORK/graph" "$WORK/tiles" "$WORK/vietnam.osm.pbf.part" "$WORK/highways.osm.pbf" "$WORK/import.osm.pbf"

echo "[refresh] download $PBF_URL"
curl -fL --retry 3 --speed-limit 10000 --speed-time 120 --max-time 3600 -o "$WORK/vietnam.osm.pbf.part" "$PBF_URL"
mv "$WORK/vietnam.osm.pbf.part" "$WORK/vietnam.osm.pbf"

echo "[refresh] expressway pre-filter"
# Drop motorroad=yes / motorcycle=no ways (e.g. trunk expressways) from the routing input only; tiles keep them.
run --memory=4g "$IMAGE" osmium tags-filter -i --overwrite -o "$WORK/import.osm.pbf" "$WORK/vietnam.osm.pbf" w/motorroad=yes w/motorcycle=no

echo "[refresh] graphhopper import"
run --memory=4g -w /app "$IMAGE" java -Xmx3g \
  -Ddw.graphhopper.datareader.file="$WORK/import.osm.pbf" \
  -Ddw.graphhopper.graph.location="$WORK/graph" \
  -jar graphhopper-web-11.1.jar import config.yml

echo "[refresh] tiles"
run --memory=4g "$IMAGE" bash -euo pipefail -c "
  osmium tags-filter -o '$WORK/highways.osm.pbf' --overwrite '$WORK/vietnam.osm.pbf' w/highway
  mkdir -p '$WORK/tiles'
  osmium export -f geojsonseq '$WORK/highways.osm.pbf' | node /app/tiles/build-tiles.mjs '$WORK/tiles'
"
[ -d "$WORK/graph" ] && [ -d "$WORK/tiles/15" ] || { echo "[refresh] build incomplete" >&2; exit 1; }

stamp=$(date +%Y%m%d%H%M%S)
prev_graph=$(readlink "$LIVE/graph" 2>/dev/null | sed 's|^releases/||' || true)
prev_tiles=$(readlink "$LIVE/tiles" 2>/dev/null | sed 's|^releases/||' || true)

echo "[refresh] swap live data ($stamp)"
mv "$WORK/graph" "$REL/graph-$stamp"
mv "$WORK/tiles" "$REL/tiles-$stamp"
rm -f "$WORK/vietnam.osm.pbf" "$WORK/highways.osm.pbf" "$WORK/import.osm.pbf"
flip graph "graph-$stamp"
flip tiles "tiles-$stamp"

healthy() {
  for _ in $(seq 1 60); do
    if $DC -f "$COMPOSE" exec -T graphhopper curl -fs "http://localhost:8989$HEALTH_PATH" >/dev/null 2>&1; then return 0; fi
    sleep 5
  done
  return 1
}

if [ -f "$COMPOSE" ] && [ -n "$($DC -f "$COMPOSE" ps -aq graphhopper 2>/dev/null)" ]; then
  echo "[refresh] restart graphhopper"
  if ! $DC -f "$COMPOSE" restart graphhopper || ! healthy; then
    echo "[refresh] graphhopper restart/health failed; rolling back" >&2
    if [ -z "$prev_graph" ] || [ -z "$prev_tiles" ]; then
      echo "[refresh] no previous data to roll back to" >&2
      exit 1
    fi
    flip graph "$prev_graph"
    flip tiles "$prev_tiles"
    rm -rf "$REL/graph-$stamp" "$REL/tiles-$stamp"
    if ! $DC -f "$COMPOSE" restart graphhopper; then
      echo "[refresh] ROLLBACK RESTART FAILED: previous data is linked but graphhopper is not running" >&2
      exit 1
    fi
    if HEALTH_PATH=/health healthy; then
      echo "[refresh] previous data restored and healthy" >&2
    else
      echo "[refresh] previous data restored but still not healthy" >&2
    fi
    exit 1
  fi
else
  echo "[refresh] no graphhopper service yet; data installed only"
fi

# success: drop every release that is not live
for n in graph tiles; do
  cur=$(readlink "$LIVE/$n" | sed 's|^releases/||')
  for d in "$REL/$n-"*; do [ "$(basename "$d")" = "$cur" ] || rm -rf "$d"; done
done
echo "[refresh] done"
