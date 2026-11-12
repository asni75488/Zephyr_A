#!/bin/sh
# Zephyr_A - certbot deploy hook
# Installed to /etc/letsencrypt/renewal-hooks/deploy/zephyr-a.sh by
# cloud/scripts/30-certs.sh. Certbot runs every script in that folder as
# root after each SUCCESSFUL renewal, with RENEWED_LINEAGE set to the
# certificate's live directory. It can also be run by hand (no variable set).
#
# Why: /etc/letsencrypt/live is readable by root only, but Mosquitto reads
# its key AFTER dropping privileges to the "mosquitto" user, and Node-RED
# runs as a normal user. The hook copies the files with the right owners
# and restarts the services, so renewals need no manual work.
set -eu

ENV_FILE=/etc/default/zephyr-a
[ -r "$ENV_FILE" ] || exit 0
# shellcheck source=/dev/null
. "$ENV_FILE"
[ -n "${CERT_DIR:-}" ] || exit 0

# Only react to our certificate.
if [ -n "${RENEWED_LINEAGE:-}" ] && [ "$RENEWED_LINEAGE" != "$CERT_DIR" ]; then
	exit 0
fi

# --- Mosquitto -----------------------------------------------------------
if [ -d /etc/mosquitto ] && getent group mosquitto >/dev/null; then
	install -d -m 0755 /etc/mosquitto/certs
	install -m 0644 -o root -g mosquitto "$CERT_DIR/fullchain.pem" /etc/mosquitto/certs/fullchain.pem
	install -m 0640 -o root -g mosquitto "$CERT_DIR/privkey.pem" /etc/mosquitto/certs/privkey.pem
	if systemctl is-active --quiet mosquitto; then
		systemctl restart mosquitto
	fi
fi

# --- Node-RED ------------------------------------------------------------
if [ -n "${NR_USER:-}" ]; then
	NR_HOME=$(getent passwd "$NR_USER" | cut -d: -f6)
	if [ -n "$NR_HOME" ] && [ -d "$NR_HOME/.node-red" ]; then
		install -d -m 0700 -o "$NR_USER" -g "$NR_USER" "$NR_HOME/.node-red/certs"
		install -m 0644 -o "$NR_USER" -g "$NR_USER" "$CERT_DIR/fullchain.pem" "$NR_HOME/.node-red/certs/fullchain.pem"
		install -m 0600 -o "$NR_USER" -g "$NR_USER" "$CERT_DIR/privkey.pem" "$NR_HOME/.node-red/certs/privkey.pem"
		if systemctl is-active --quiet nodered; then
			systemctl restart nodered
		fi
	fi
fi

logger -t zephyr-a "TLS certificate deployed from $CERT_DIR"
