#!/usr/bin/env bash
# Zephyr_A - 60: add (or change the password of) an edge node / device.
#
#   sudo ./60-add-device.sh esp32c6-14c19fe0ab24
#   sudo ./60-add-device.sh esp32c6-test0001      (user for the test publisher)
#
# The MQTT user name IS the Sparkplug edge node id - the ACL
# (/etc/mosquitto/acl, "pattern ... %u") then allows the device to use only
# its own topics. The device id is printed at boot ("Device id: ...").
# The password goes into app/secrets.conf on the PC (never into git).

# shellcheck source=common.sh
. "$(dirname "$0")/common.sh"
require_root

ID="${1:-}"
[ -n "$ID" ] || die "Usage: sudo $0 <edge-node-id>   e.g. esp32c6-14c19fe0ab24"
[[ "$ID" =~ ^[A-Za-z0-9._-]{1,64}$ ]] || die "Invalid id '$ID' (allowed: A-Z a-z 0-9 . _ -)"
[ -f /etc/mosquitto/passwd ] || die "Run ./40-mosquitto.sh first"

info "MQTT user '$ID'"
if grep -q "^${ID}:" /etc/mosquitto/passwd; then
	warn "user exists - setting a new password"
fi
echo "    Use a long random password, e.g. from: openssl rand -base64 18"
mosquitto_passwd /etc/mosquitto/passwd "$ID"
chown mosquitto:mosquitto /etc/mosquitto/passwd
chmod 0600 /etc/mosquitto/passwd

# SIGHUP: Mosquitto re-reads passwd and acl without dropping connections.
systemctl reload mosquitto
ok "password stored, broker reloaded"

cat <<EOF

    The device may publish on (ACL):
      spBv1.0/ProjektA/{NBIRTH,NDEATH,NDATA}/$ID
      spBv1.0/ProjektA/{DBIRTH,DDEATH,DDATA}/$ID/<device>
    and subscribe to:
      spBv1.0/ProjektA/NCMD/$ID
      spBv1.0/ProjektA/DCMD/$ID/<device>

    Put the password in app\\secrets.conf on the PC (part 6):
      CONFIG_APP_MQTT_USERNAME="$ID"
      CONFIG_APP_MQTT_PASSWORD="<the password>"
EOF
