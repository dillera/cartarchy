#!/usr/bin/env bash
# Build the server for the Raspberry Pi and install it as a systemd service.
# Usage: deploy/deploy.sh [host]    (default host: actual)
set -euo pipefail

host="${1:-actual}"
remote_dir=/home/dillera/_services/cartarchy
server_dir="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

cd "$server_dir"
go test ./...
GOOS=linux GOARCH=arm64 CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o "$build_dir/cartarchy-server" .

ssh "$host" mkdir -p "$remote_dir/data"
scp -q "$build_dir/cartarchy-server" "$host:$remote_dir/cartarchy-server.new"
scp -q deploy/cartarchy-server.service deploy/cartarchy.env.example \
	deploy/nginx-acme.conf deploy/nginx-tls.conf deploy/frpc-cartarchy.toml "$host:$remote_dir/"

ssh "$host" bash -s -- "$remote_dir" <<'REMOTE'
set -euo pipefail
cd "$1"
if [ ! -f cartarchy.env ]; then
	pairing_code="$(openssl rand -hex 4)"
	admin_password="$(openssl rand -base64 24 | tr -d '/+=')"
	sed -e "s/^CARTARCHY_PAIRING_CODE=.*/CARTARCHY_PAIRING_CODE=$pairing_code/" \
		-e "s/^CARTARCHY_ADMIN_PASSWORD=.*/CARTARCHY_ADMIN_PASSWORD=$admin_password/" \
		cartarchy.env.example > cartarchy.env
	chmod 600 cartarchy.env
	echo "Created $1/cartarchy.env with a new pairing code and admin password."
fi
mv cartarchy-server.new cartarchy-server
sudo install -m 644 cartarchy-server.service /etc/systemd/system/cartarchy-server.service
sudo systemctl daemon-reload
sudo systemctl enable --quiet cartarchy-server
sudo systemctl restart cartarchy-server

listen="$(sed -n 's/^CARTARCHY_LISTEN=//p' cartarchy.env)"
for _ in $(seq 1 20); do
	if curl -fs "http://$listen/health"; then
		echo
		exit 0
	fi
	sleep 0.5
done
sudo journalctl -u cartarchy-server -n 30 --no-pager
exit 1
REMOTE
