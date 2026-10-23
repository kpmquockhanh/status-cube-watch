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
2. Phase 1 (no certificate yet): temporarily copy `nginx/phase1/nav-http.conf` over `nginx/nav.conf` on the host, then
   `docker compose -f /root/nav-server/docker-compose.yml up -d nginx certbot`. `nav.devxdev.site` must have a DNS record (proxied) first.
3. Certificate: `docker compose -f /root/nav-server/docker-compose.yml run --rm --entrypoint certbot certbot certonly --webroot -w /var/www/certbot -d nav.devxdev.site`
4. Phase 2: restore the full `nginx/nav.conf`, then `docker exec nav-server-nginx-1 nginx -t && docker exec nav-server-nginx-1 nginx -s reload`.
   Later config edits: the same.
5. `docker compose -f /root/nav-server/docker-compose.yml up -d`; `cp cron/nav-refresh /etc/cron.d/`.
6. Test (from a machine with `node`; the host has none): `NAV_KEY=... ./smoke.sh` (optional `NAV_URL`; `SAVE=path` stores the HCMC route response).
   It includes a 300-tile corridor burst.

## Monthly refresh
`/etc/cron.d/nav-refresh`: 03:30 on day 3, runs `timeout 4h refresh.sh` (about 17 min; import peaks near 2.5 GB heap,
graph 569 MB, tiles 1.6 GB; see `NOTES.md`). The download aborts if it stalls (under 10 kB/s for 2 min) or exceeds 1 h, so the lock is never held forever.
Ways tagged `motorroad=yes` or `motorcycle=no` are removed from the routing input only (tiles keep them). It flips data atomically and rolls back if GraphHopper is unhealthy.
Log: `/var/log/nav-refresh.log`.

## Removal
`docker compose -f /root/nav-server/docker-compose.yml down`, remove `/etc/cron.d/nav-refresh`, then `/root/nav` and `/root/nav-server`.
