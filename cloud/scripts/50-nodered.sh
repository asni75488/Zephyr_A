#!/usr/bin/env bash
# Zephyr_A - 50: Node-RED (Sparkplug B decoding, data log, dashboard).
#
#   sudo ./50-nodered.sh
#
# 1. Installs Node.js + Node-RED with the official installer
#    (github.com/node-red/linux-installers) as a systemd service running as
#    $NR_USER (default: the user that runs sudo, e.g. admin).
# 2. Installs Dashboard 2.0 (@flowfuse/node-red-dashboard).
# 3. Asks for editor and dashboard logins, stores bcrypt hashes + a random
#    credentialSecret in ~/.node-red/zephyr_a-secrets.json (chmod 600).
# 4. Activates cloud/node-red/settings-zephyr_a.js (HTTPS, logins).
# 5. Copies the TLS certificate (certbot deploy hook) and starts Node-RED.
#
# Afterwards import cloud/node-red/flows.json in the editor (see docs/05-cloud.md).

# shellcheck source=common.sh
. "$(dirname "$0")/common.sh"
require_root

[ -n "${CERT_DIR:-}" ] || die "CERT_DIR not set - run ./30-certs.sh first"
NR_HOME="$(user_home "$NR_USER")"
[ -n "$NR_HOME" ] || die "User $NR_USER does not exist"
NR_DIR="$NR_HOME/.node-red"
DATA_DIR="$NR_HOME/zephyr_a-data"

as_user() { sudo -u "$NR_USER" -H "$@"; }

info "Node-RED for user $NR_USER ($NR_DIR)"
if command -v node-red >/dev/null; then
	ok "already installed: $(node-red --version 2>/dev/null | tr '\n' ' ' | head -c 80)"
else
	installer=$(mktemp)
	curl -fsSL -o "$installer" \
		https://github.com/node-red/linux-installers/releases/latest/download/install-update-nodered-deb
	bash "$installer" --confirm-root --confirm-install --skip-pi --no-init --node24 \
		--nodered-user="$NR_USER"
	rm -f "$installer"
	command -v node-red >/dev/null || die "Node-RED installation failed (see /var/log/nodered-install.log)"
	ok "installed"
fi
systemctl stop nodered 2>/dev/null || true

info "User directory"
as_user mkdir -p "$NR_DIR" "$DATA_DIR"
if [ ! -f "$NR_DIR/settings.js" ]; then
	as_user cp "$(npm root -g)/node-red/settings.js" "$NR_DIR/settings.js"
	ok "default settings.js copied"
fi
install -m 0644 -o "$NR_USER" -g "$NR_USER" "$CLOUD_DIR/node-red/settings-zephyr_a.js" "$NR_DIR/settings-zephyr_a.js"
ok "settings-zephyr_a.js installed"

info "Dashboard 2.0"
if [ -d "$NR_DIR/node_modules/@flowfuse/node-red-dashboard" ]; then
	ok "already installed"
else
	(cd "$NR_DIR" && as_user npm install --no-fund --no-audit @flowfuse/node-red-dashboard)
	ok "installed"
fi

info "Logins (stored as bcrypt hashes in $NR_DIR/zephyr_a-secrets.json)"
SECRETS="$NR_DIR/zephyr_a-secrets.json"
if [ -f "$SECRETS" ]; then
	ok "already configured (delete $SECRETS and re-run to change)"
else
	BCRYPT_PATH="$(npm root -g)/node-red/node_modules"
	[ -d "$BCRYPT_PATH/bcryptjs" ] || die "bcryptjs not found in $BCRYPT_PATH"
	hash_pw() {
		printf '%s' "$1" | NODE_PATH="$BCRYPT_PATH" node -e '
			const b = require("bcryptjs"); let d = "";
			process.stdin.on("data", c => d += c).on("end", () => console.log(b.hashSync(d, 10)));'
	}
	read -r -p "    Editor user name [admin]: " ADMIN_USER
	ADMIN_USER="${ADMIN_USER:-admin}"
	read_password ADMIN_PW "Editor password"
	read -r -p "    Dashboard user name [viewer]: " DASH_USER
	DASH_USER="${DASH_USER:-viewer}"
	read_password DASH_PW "Dashboard password"
	ADMIN_HASH="$(hash_pw "$ADMIN_PW")"
	DASH_HASH="$(hash_pw "$DASH_PW")"
	unset ADMIN_PW DASH_PW
	CRED_SECRET="$(openssl rand -hex 32)"
	umask 077
	ADMIN_USER="$ADMIN_USER" ADMIN_HASH="$ADMIN_HASH" DASH_USER="$DASH_USER" \
		DASH_HASH="$DASH_HASH" CRED_SECRET="$CRED_SECRET" node -e '
		const e = process.env;
		console.log(JSON.stringify({ adminUser: e.ADMIN_USER, adminHash: e.ADMIN_HASH,
			dashUser: e.DASH_USER, dashHash: e.DASH_HASH,
			credentialSecret: e.CRED_SECRET }, null, 2));' >"$SECRETS"
	chown "$NR_USER:$NR_USER" "$SECRETS"
	chmod 0600 "$SECRETS"
	ok "created"
fi

info "Service environment ($NR_DIR/environment)"
cat >"$NR_DIR/environment" <<EOF
# Read by nodered.service (EnvironmentFile). Managed by cloud/scripts/50-nodered.sh
NODE_RED_OPTIONS=--settings $NR_DIR/settings-zephyr_a.js
ZEPHYR_A_DATA_DIR=$DATA_DIR
EOF
chown "$NR_USER:$NR_USER" "$NR_DIR/environment"
chmod 0644 "$NR_DIR/environment"
ok "NODE_RED_OPTIONS and ZEPHYR_A_DATA_DIR set"

info "TLS certificate"
RENEWED_LINEAGE="$CERT_DIR" /etc/letsencrypt/renewal-hooks/deploy/zephyr-a.sh
ok "copied to $NR_DIR/certs"

info "Start Node-RED"
systemctl daemon-reload
systemctl enable nodered >/dev/null 2>&1
systemctl restart nodered
for _ in $(seq 1 30); do
	if curl -sk -o /dev/null "https://127.0.0.1:1880/"; then
		break
	fi
	sleep 2
done
if systemctl is-active --quiet nodered && curl -sk -o /dev/null "https://127.0.0.1:1880/"; then
	ok "running: https://$DOMAIN:1880/  (editor)   https://$DOMAIN:1880/dashboard  (dashboard)"
else
	journalctl -u nodered -n 40 --no-pager | sed 's/^/    /'
	die "Node-RED did not start (see log above)"
fi

info "Done. Next: import cloud/node-red/flows.json in the editor (docs/05-cloud.md), then ./90-verify.sh"
