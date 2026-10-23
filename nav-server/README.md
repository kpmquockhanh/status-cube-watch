# nav-server

Routing + street tiles for the motorbike nav app, on the `contabo` host, served at
`https://nav.devxdev.site` (Cloudflare proxied) through the existing n8n nginx container.

## What runs where
- `graphhopper` (this `docker-compose.yml`, image `nav-graphhopper:11.1`, `-Xmx3g`, no published ports,
  on network `n8n_default`). Data: `/root/nav/data` mounted at `/data` (graph is a symlink flipped by
  `refresh.sh`, so the whole directory is mounted; rw because GraphHopper writes `gh.lock`).
- n8n's nginx (`/root/n8n/docker-compose.prod.yml`) gets four extra mounts, directories except one static file:
  `nginx/include/00-nav-include.conf` -> `conf.d/00-nav-include.conf` (static, `include /etc/nginx/nav/*.conf;`, sorts before `default.conf`),
  `/root/nav-server/nginx` -> `/etc/nginx/nav` (holds `00-nav.conf` with `map_hash_bucket_size`, loaded first, and `nav.conf`),
  `/root/nav/nginx-key` -> `/etc/nginx/nav-key` (holds `nav-key.conf`), `/root/nav/data` -> `/srv/nav-data`.
  Directory mounts mean edits and new files are seen by a reload (no restart). Routes: `POST /route`, `GET /health` (to GraphHopper),
  `GET /tiles/{z}/{x}/{y}.json` (static files). Every request needs header `X-Nav-Key`; tiles are rate limited per key
  (50 r/s, burst 2000, over-limit 429) so a long-ride corridor prefetch is not throttled.
- Certificate: certbot of the n8n stack (`/root/n8n/certbot`), renewed by its loop.

## The key
Lives only on the server, `/root/nav/nginx-key/nav-key.conf` (mode 600). Read it with:

    ssh contabo cat /root/nav/nginx-key/nav-key.conf

Rotate: write a new file in the same format (see `nginx/nav-key.conf.example`), then
`docker exec n8n-nginx-1 nginx -t && docker exec n8n-nginx-1 nginx -s reload`.

## Deploy
1. Copy: `COPYFILE_DISABLE=1 tar -C nav-server -cf - . | ssh contabo 'tar -C /root/nav-server -xf -'`, then `chown -R root:root /root/nav-server`.
2. Back up `/root/n8n/docker-compose.prod.yml` (e.g. `.bak-nav2`); add the four nginx mounts above to the `nginx` service.
3. Phase 1 (no certificate yet): put `nginx/phase1/nav-http.conf` in a scratch dir and mount that as `/etc/nginx/nav` instead (or temporarily
   copy it over `nginx/nav.conf` on the host), then `docker compose -f docker-compose.prod.yml up -d nginx`.
   `nav.devxdev.site` must have a DNS record (proxied) first.
4. Certificate (from `/root/n8n`): `docker compose -f docker-compose.prod.yml run --rm --entrypoint certbot certbot certonly --webroot -w /var/www/certbot -d nav.devxdev.site`
5. Phase 2: restore the full `nginx/nav.conf`. Before touching the shared container, validate with a throwaway one on the same network and mounts
   (`docker run --rm --network n8n_default -e DOMAIN=... <the nginx mounts of the compose file> nginx:1.27-alpine nginx -t`).
   Only if that passes: `docker compose -f docker-compose.prod.yml up -d nginx` (needed once, when the mounts change).
   Later config edits: edit in place, then `docker exec n8n-nginx-1 nginx -t && docker exec n8n-nginx-1 nginx -s reload`.
6. `docker compose -f /root/nav-server/docker-compose.yml up -d`; `cp cron/nav-refresh /etc/cron.d/`.
7. After every nginx change: `curl -sI https://n8n.devxdev.site/` must still say 401.
8. Test: `NAV_KEY=... ./smoke.sh` (optional `NAV_URL`; `SAVE=path` stores the HCMC route response). It includes a 300-tile corridor burst.

## Monthly refresh
`/etc/cron.d/nav-refresh`: 03:30 on day 3, runs `timeout 4h refresh.sh` (about 17 min; import peaks near 2.5 GB heap,
graph 569 MB, tiles 1.6 GB; see `NOTES.md`). The download aborts if it stalls (under 10 kB/s for 2 min) or exceeds 1 h, so the lock is never held forever.
Ways tagged `motorroad=yes` or `motorcycle=no` are removed from the routing input only (tiles keep them). It flips data atomically and rolls back if GraphHopper is unhealthy.
Log: `/var/log/nav-refresh.log`.

## Rollback
Restore `docker-compose.prod.yml.bak-nav2` over `docker-compose.prod.yml`, `docker compose -f docker-compose.prod.yml up -d nginx`,
then `docker compose -f /root/nav-server/docker-compose.yml down` and remove `/etc/cron.d/nav-refresh`.
