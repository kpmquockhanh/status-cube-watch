# nav-server

Routing + street tiles for the motorbike nav app, on the `contabo` host, served at
`https://nav.devxdev.site` (Cloudflare proxied) through the existing n8n nginx container.

## What runs where
- `graphhopper` (this `docker-compose.yml`, image `nav-graphhopper:11.1`, `-Xmx3g`, no published ports,
  on network `n8n_default`). Data: `/root/nav/data` mounted at `/data` (graph is a symlink flipped by
  `refresh.sh`, so the whole directory is mounted; rw because GraphHopper writes `gh.lock`).
- n8n's nginx (`/root/n8n/docker-compose.prod.yml`) gets three extra mounts:
  `nginx/nav.conf` -> `/etc/nginx/conf.d/nav.conf`, `/root/nav/nav-key.conf` -> `/etc/nginx/nav-key.conf`,
  `/root/nav/data` -> `/srv/nav-data`. Routes: `POST /route`, `GET /health` (to GraphHopper),
  `GET /tiles/{z}/{x}/{y}.json` (static files). Every request needs header `X-Nav-Key`.
- Certificate: certbot of the n8n stack (`/root/n8n/certbot`), renewed by its loop.

## The key
Lives only on the server, `/root/nav/nav-key.conf` (mode 600). Read it with:

    ssh contabo cat /root/nav/nav-key.conf

Rotate: write a new file in the same format (see `nginx/nav-key.conf.example`), then
`docker exec n8n-nginx-1 nginx -s reload`.

## Deploy
1. Copy: `tar -C nav-server -cf - . | ssh contabo 'tar -C /root/nav-server -xf -'`
2. Back up `/root/n8n/docker-compose.prod.yml` as `.bak-nav`; add the three nginx mounts above.
3. Phase 1: copy `nginx/nav-http.conf` to `/root/nav-server/nginx/nav.conf`, `docker compose -f docker-compose.prod.yml up -d nginx`.
   `nav.devxdev.site` must have a DNS record (proxied) first.
4. Certificate (from `/root/n8n`): `docker compose -f docker-compose.prod.yml run --rm --entrypoint certbot certbot certonly --webroot -w /var/www/certbot -d nav.devxdev.site`
5. Phase 2: copy the full `nginx/nav.conf` to `/root/nav-server/nginx/nav.conf`, `docker exec n8n-nginx-1 nginx -t && docker exec n8n-nginx-1 nginx -s reload`.
6. `docker compose -f /root/nav-server/docker-compose.yml up -d`; `cp cron/nav-refresh /etc/cron.d/`.
7. After every nginx change: `curl -sI https://n8n.devxdev.site/` must still say 401.
8. Test: `NAV_KEY=... ./smoke.sh` (optional `NAV_URL`; `SAVE=path` stores the HCMC route response).

## Monthly refresh
`/etc/cron.d/nav-refresh`: 03:30 on day 3, runs `refresh.sh` (about 17 min; import peaks near 2.5 GB heap,
graph 569 MB, tiles 1.6 GB; see `NOTES.md`). It flips data atomically and rolls back if GraphHopper is unhealthy.
Log: `/var/log/nav-refresh.log`.

## Rollback
Restore `docker-compose.prod.yml.bak-nav` over `docker-compose.prod.yml`, `docker compose -f docker-compose.prod.yml up -d nginx`,
then `docker compose -f /root/nav-server/docker-compose.yml down` and remove `/etc/cron.d/nav-refresh`.
