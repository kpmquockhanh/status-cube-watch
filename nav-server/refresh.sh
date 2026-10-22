#!/usr/bin/env bash
# Rebuild the routing graph and street tiles from a fresh Vietnam extract.
# Live data is only replaced when every phase succeeded. Safe for cron.
set -euo pipefail

PBF_URL="${PBF_URL:-https://download.geofabrik.de/asia/vietnam-latest.osm.pbf}"
NAV=/root/nav
IMAGE=nav-graphhopper:11.1
COMPOSE=/root/nav-server/docker-compose.yml
WORK=$NAV/work
LIVE=$NAV/data

mkdir -p "$WORK" "$LIVE"
rm -rf "$WORK/graph" "$WORK/tiles" "$WORK/vietnam.osm.pbf.part" "$WORK/highways.osm.pbf"

run() { docker run --rm -v "$NAV:$NAV" "$@"; }

echo "[refresh] download $PBF_URL"
curl -fL --retry 3 -o "$WORK/vietnam.osm.pbf.part" "$PBF_URL"
mv "$WORK/vietnam.osm.pbf.part" "$WORK/vietnam.osm.pbf"

echo "[refresh] graphhopper import"
run -w /app "$IMAGE" java -Xmx5g \
  -Ddw.graphhopper.datareader.file="$WORK/vietnam.osm.pbf" \
  -Ddw.graphhopper.graph.location="$WORK/graph" \
  -jar graphhopper-web-11.1.jar import config.yml

echo "[refresh] tiles"
run "$IMAGE" bash -euo pipefail -c "
  osmium tags-filter -o '$WORK/highways.osm.pbf' --overwrite '$WORK/vietnam.osm.pbf' w/highway
  mkdir -p '$WORK/tiles'
  osmium export -f geojsonseq '$WORK/highways.osm.pbf' | node /app/tiles/build-tiles.mjs '$WORK/tiles'
"
[ -d "$WORK/graph" ] && [ -d "$WORK/tiles/15" ] || { echo "[refresh] build incomplete" >&2; exit 1; }

echo "[refresh] swap live data"
rm -rf "$LIVE/graph.old" "$LIVE/tiles.old"
[ -e "$LIVE/graph" ] && mv "$LIVE/graph" "$LIVE/graph.old"
[ -e "$LIVE/tiles" ] && mv "$LIVE/tiles" "$LIVE/tiles.old"
mv "$WORK/graph" "$LIVE/graph"
mv "$WORK/tiles" "$LIVE/tiles"
rm -f "$WORK/vietnam.osm.pbf" "$WORK/highways.osm.pbf"

if [ -f "$COMPOSE" ] && [ -n "$(docker compose -f "$COMPOSE" ps -aq graphhopper 2>/dev/null)" ]; then
  echo "[refresh] restart graphhopper"
  docker compose -f "$COMPOSE" restart graphhopper
  ok=0
  for _ in $(seq 1 60); do
    if docker compose -f "$COMPOSE" exec -T graphhopper curl -fs http://localhost:8989/health >/dev/null 2>&1; then ok=1; break; fi
    sleep 5
  done
  if [ "$ok" != 1 ]; then
    echo "[refresh] graphhopper not healthy; old data kept in $LIVE/*.old" >&2
    exit 1
  fi
else
  echo "[refresh] no graphhopper service yet; data installed only"
fi
rm -rf "$LIVE/graph.old" "$LIVE/tiles.old"
echo "[refresh] done"
