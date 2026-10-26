# Cartarchy game server

The server provides mobile pairing, player profiles, public game lobbies, durable turn-based games, real-time updates, leaderboards, and a browser-based admin console. All persistent state is stored in SQLite.

## Run locally

Requires Go 1.25. The SQLite driver is pure Go, so the Linux build does not require CGO or system SQLite packages.

```sh
go run . \
  -listen 0.0.0.0:18080 \
  -pairing-code harness \
  -data /tmp/cartarchy-local.sqlite
```

Open the server through the concrete address the phone can reach, such as
`http://192.168.1.233:18080`. Pairing and sharing QR codes dynamically use the
address through which each page or QR was requested.

Open that address for:

- the server pairing QR;
- the leaderboard and waiting/active games;
- a form that creates a shareable game lobby and QR.

The temporary admin credentials are:

```text
username: admin
password: cartarchy
```

They can be overridden with `CARTARCHY_ADMIN_USERNAME` and `CARTARCHY_ADMIN_PASSWORD` or the corresponding command-line flags. Basic Auth must only be used over HTTPS outside a trusted local network.

The optional `public-url` override must be reachable from the phone. Production installations should set it to the canonical HTTPS address and persist the directory containing the SQLite database. SQLite foreign keys, WAL journaling, and a five-second busy timeout are enabled at connection time.

## JSON migration

If `-data` points at an existing database from the previous JSON-backed server, startup detects the JSON, moves it to `<path>.legacy.json`, creates SQLite at the original path, assigns UUIDs and profile secrets, and imports players, devices, games, events, and records. The backup is retained.

## Turn-based persistence

See [SCHEMA.md](SCHEMA.md) for the schema and round/turn invariants. Briefly, game metadata, seats, rounds, turns, events, and the latest resumable game state are separate transactional records. A `turn.completed` request closes the current turn and opens its successor atomically.

An active participant can resign with `POST /v1/games/{id}/resign`. The server
removes that seat, advances control when necessary, publishes a live
`game.resigned` update, and awards the remaining player a win in a two-player
game.

## HTTP surfaces

- `/` — public dashboard, leaderboard, pairing QR, and game creation.
- `/games/{id}` — public lobby and shareable join QR.
- `/profiles/{id}?key=…` — private capability URL for profile editing and profile QR.
- `/admin` — Basic Auth administration for games, players, and devices.
- `/v1/...` — bearer-token mobile API.

## Deploying to `actual`

The production server runs on the Raspberry Pi `actual` as the
`cartarchy-server` systemd service, under `~/_services/cartarchy`. Public
traffic arrives through the frp edge (`qix.diller.org`): frpc passes TLS
through to nginx on the Pi, and nginx proxies to the server on
`127.0.0.1:18080`. Release builds of both apps require HTTPS.

```text
phone ──https──▶ frps (qix) ──frpc──▶ nginx :8943 (TLS) ──▶ cartarchy-server 127.0.0.1:18080
       ──http───▶ frps (qix) ──frpc──▶ nginx :8902 (ACME + redirect)
```

Each release is a single command from this directory. It runs the tests,
cross-compiles a static `linux/arm64` binary (the Pi's Go is too old to build
it), installs it, and checks `/health`:

```sh
deploy/deploy.sh            # or: deploy/deploy.sh <host>
```

The first run creates `~/_services/cartarchy/cartarchy.env` from
[`deploy/cartarchy.env.example`](deploy/cartarchy.env.example) with a random
pairing code and admin password. Later runs never touch it, and the SQLite data
in `~/_services/cartarchy/data` is kept.

One-time edge setup, after the first deploy. Run it on `actual` from
`~/_services/cartarchy`, where the deploy copies the nginx and frpc files:

1. At DreamHost, add `cartarchy.diller.org` as a CNAME for `qix.diller.org`.
2. Install the ACME/redirect site and reload nginx:
   ```sh
   sudo mkdir -p /var/www/certbot
   sudo cp nginx-acme.conf /etc/nginx/sites-available/cartarchy-acme.conf
   sudo ln -s ../sites-available/cartarchy-acme.conf /etc/nginx/sites-enabled/cartarchy-acme
   sudo nginx -t && sudo systemctl reload nginx
   ```
3. Append [`deploy/frpc-cartarchy.toml`](deploy/frpc-cartarchy.toml) to
   `~/_services/frpc/frpc.toml` and run `sudo systemctl restart frpc`. The
   restart briefly reconnects every service that frp proxies.
4. Issue the certificate. Debian's certbot timer renews it and reloads nginx:
   ```sh
   sudo apt install certbot
   sudo certbot certonly --webroot -w /var/www/certbot -d cartarchy.diller.org \
     --deploy-hook "systemctl reload nginx"
   ```
5. Install the TLS site the same way as step 2, using `nginx-tls.conf` as
   `cartarchy-tls`, then check `curl https://cartarchy.diller.org/health`.

Operate it with `systemctl status cartarchy-server` and
`journalctl -u cartarchy-server -f`. The admin credentials and pairing code
are in `cartarchy.env`.

`/v1/events` sends a `: keepalive` comment every 25 seconds. Idle streams stay
under the 60-second timeouts of `URLSession` and reverse proxies, and the apps
ignore the comments.

## Verification

Run the integration suite with the race detector and coverage:

```sh
go test -race -coverprofile=coverage.out .
go test ./...
go tool cover -func=coverage.out
```

Run the black-box harness against a live server:

```sh
go run ./cmd/harness \
  -server http://127.0.0.1:18080 \
  -pairing-code harness \
  -admin-username admin \
  -admin-password cartarchy
```
