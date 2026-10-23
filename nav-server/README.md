# nav-server

Routing + street tiles for the motorbike nav app, on the `contabo` host, served at
`https://nav.devxdev.site` (Cloudflare proxied). Everything runs from this `docker-compose.yml`; nothing else is on the host.

## What runs where
- `graphhopper` (image `nav-graphhopper:11.1`, `-Xmx4500m` in 5500m, no published ports). Data: `/root/nav/data` mounted at `/data`
  (graph is a symlink flipped by `refresh.sh`, so the whole directory is mounted; rw because GraphHopper writes `gh.lock`).
- `nginx` (ports 80/443), mounts, directories except one static file:
  `nginx/include/00-nav-include.conf` -> `conf.d/default.conf` (static, `include /etc/nginx/nav/*.conf;`, replaces the image's default server),
  `/root/nav-server/nginx` -> `/etc/nginx/nav` (holds `00-nav.conf` with `map_hash_bucket_size`, loaded first, and `nav.conf`),
  `/root/nav/nginx-key` -> `/etc/nginx/nav-key` (holds `nav-key.conf`), `/root/nav/data` -> `/srv/nav-data`,
  `/root/nav/certbot/{conf,www}` -> `/etc/letsencrypt`, `/var/www/certbot`.
  Directory mounts mean edits and new files are seen by a reload (no restart). Routes: `POST /route`, `GET /health` (to GraphHopper),
  `GET /tiles/{z}/{x}/{y}.json` (static files). Every request needs header `X-Nav-Key`; tiles are rate limited per key
  (50 r/s, burst 2000, over-limit 429) so a long-ride corridor prefetch is not throttled.
- `certbot`: renews the certificate in `/root/nav/certbot/conf` every 12 h (webroot).

## The key
Lives only on the server, `/root/nav/nginx-key/nav-key.conf` (mode 600). Read it with:

    ssh contabo cat /root/nav/nginx-key/nav-key.conf

Rotate: write a new file in the same format (see `nginx/nav-key.conf.example`), then
`docker exec nav-server-nginx-1 nginx -t && docker exec nav-server-nginx-1 nginx -s reload`.

## Deploy
1. Copy: `COPYFILE_DISABLE=1 tar -C nav-server -cf - . | ssh contabo 'tar -C /root/nav-server -xf -'`, then `chown -R root:root /root/nav-server`.
   Image (again whenever `graphhopper/Dockerfile`, `config.yml`, `moto_base.json` or `style_*.json` change):
   `docker build -f /root/nav-server/graphhopper/Dockerfile /root/nav-server -t nav-graphhopper:11.1`. A changed profile list or
   encoded value needs a new graph, so then run `refresh.sh`, which recreates the container on the new image and graph together.
2. Phase 1 (no certificate yet): temporarily copy `nginx/phase1/nav-http.conf` over `nginx/nav.conf` on the host, then
   `docker compose -f /root/nav-server/docker-compose.yml up -d nginx certbot`. `nav.devxdev.site` must have a DNS record (proxied) first.
3. Certificate: `docker compose -f /root/nav-server/docker-compose.yml run --rm --entrypoint certbot certbot certonly --webroot -w /var/www/certbot -d nav.devxdev.site`
4. Phase 2: restore the full `nginx/nav.conf`, then `docker exec nav-server-nginx-1 nginx -t && docker exec nav-server-nginx-1 nginx -s reload`.
   Later config edits: the same.
5. `docker compose -f /root/nav-server/docker-compose.yml up -d`; `cp cron/nav-refresh /etc/cron.d/`.
6. Test (from a machine with `node`; the host has none): `NAV_KEY=... ./smoke.sh` (optional `NAV_URL`; `SAVE=path` stores the HCMC route response).
   It routes every profile and includes a 300-tile corridor burst.

## Route styles
Four turn-cost CH profiles, each `moto_base.json` plus one style: `moto_vn` (Fastest, `style_fast.json`; the name older apps
ask for), `moto_calm`, `moto_calm_rush` (Calm off-peak and in rush hour) and `moto_turns` (Fewest turns). The app picks
the profile (`RouteStyle.swift`). Tuning and figures: `NOTES.md`. To check them:
- `NAV_URL=... NAV_KEY=... node styles-check.mjs` runs the trips in `trips.json` through every profile against the bars in
  `NOTES.md` (default `NAV_URL` is a local server on 8989). Two trips miss a bar, accepted: Lang Ha→Hai Ba Trung
  (Calm rush has more lights) and Ha Dong→Hoang Mai (Fewest turns has one more turn).
- `NAV_KEY=... node route-lab/server.mjs`, then http://localhost:8990: a map page that draws the styles side by side for a
  trip or two clicked points, with the stats the phone shows, and keeps 👍/👎 notes in `route-lab/verdicts.jsonl`.
  It listens on loopback only, since it spends the key.

## Monthly refresh
`/etc/cron.d/nav-refresh`: 03:30 on day 3, runs `timeout 4h refresh.sh` (the four-profile import alone took 68 min locally
and peaks near 2.9 GB of its 3 GB heap; graph 1.6 GB, tiles 1.6 GB; see `NOTES.md`). The download aborts if it stalls (under 10 kB/s for 2 min) or exceeds 1 h, so the lock is never held forever.
Ways tagged `motorroad=yes` or `motorcycle=no` are removed from the routing input only (tiles keep them). It flips data atomically, recreates the graphhopper container (so it starts with the compose file's image, heap and
memory limit) and rolls back if GraphHopper is unhealthy.
Log: `/var/log/nav-refresh.log`.

## Removal
`docker compose -f /root/nav-server/docker-compose.yml down`, remove `/etc/cron.d/nav-refresh`, then `/root/nav` and `/root/nav-server`.
