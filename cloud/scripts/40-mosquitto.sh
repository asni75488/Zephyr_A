#!/usr/bin/env bash
# Zephyr_A - 40: Mosquitto MQTT broker with TLS, users and ACL.
#
#   sudo ./40-mosquitto.sh
#
# Installs cloud/mosquitto/zephyr_a.conf and cloud/mosquitto/acl, creates the
# password file, asks for the password of the "nodered" MQTT user (used by
# Node-RED on the same server) and restarts the broker.
# Devices are added afterwards with ./60-add-device.sh <edge-node-id>.
#
# File ownership matters: Mosquitto 2.0 drops privileges to the "mosquitto"
# user before reading passwd/acl/key, and warns if the files are not owned by
# that user with no world access (verified with Mosquitto 2.0.18).

# shellcheck source=common.sh
. "$(dirname "$0")/common.sh"
require_root

command -v mosquitto >/dev/null || die "Mosquitto not installed - run ./10-base.sh first"
[ -n "${CERT_DIR:-}" ] || die "CERT_DIR not set - run ./30-certs.sh first"

info "Configuration"
install -m 0644 "$CLOUD_DIR/mosquitto/zephyr_a.conf" /etc/mosquitto/conf.d/zephyr_a.conf
install -m 0600 -o mosquitto -g mosquitto "$CLOUD_DIR/mosquitto/acl" /etc/mosquitto/acl
if [ ! -f /etc/mosquitto/passwd ]; then
	install -m 0600 -o mosquitto -g mosquitto /dev/null /etc/mosquitto/passwd
fi
chown mosquitto:mosquitto /etc/mosquitto/passwd
chmod 0600 /etc/mosquitto/passwd
ok "/etc/mosquitto/conf.d/zephyr_a.conf, /etc/mosquitto/acl, /etc/mosquitto/passwd"

info "TLS certificate for the 8883 listener"
RENEWED_LINEAGE="$CERT_DIR" /etc/letsencrypt/renewal-hooks/deploy/zephyr-a.sh
stat -c '    %A %U:%G %n' /etc/mosquitto/certs/*

info "MQTT user 'nodered' (Node-RED, local listener 127.0.0.1:1883)"
if grep -q '^nodered:' /etc/mosquitto/passwd; then
	ok "already exists (change with: sudo mosquitto_passwd /etc/mosquitto/passwd nodered)"
else
	echo "    Choose a password. You will type it again in Node-RED's MQTT broker node."
	mosquitto_passwd /etc/mosquitto/passwd nodered
	chown mosquitto:mosquitto /etc/mosquitto/passwd
	chmod 0600 /etc/mosquitto/passwd
	ok "created"
fi

info "Start broker"
systemctl enable mosquitto >/dev/null 2>&1
systemctl restart mosquitto
sleep 1
if systemctl is-active --quiet mosquitto; then
	ok "mosquitto running"
else
	journalctl -u mosquitto -n 30 --no-pager | sed 's/^/    /'
	die "mosquitto failed to start (see log above)"
fi
ss -ltnp | grep -E ':(1883|8883)\b' | awk '{print "    listening on " $4}'

info "Done. Next: sudo ./60-add-device.sh esp32c6-14c19fe0ab24   (then ./50-nodered.sh)"
