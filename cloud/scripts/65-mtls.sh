#!/usr/bin/env bash
# Zephyr_A - 65: client certificates for the devices (mTLS, part 10).
#
# On the PC (the device CA never leaves it):
#   python scripts\device_pki.py init
#   python scripts\device_pki.py issue esp32c6-14c19fe0ab24
#   scp certs\device-pki\ca.crt certs\device-pki\crl.pem admin@zephyra.vps.webdock.cloud:~/
# On the server:
#   sudo ./65-mtls.sh ~/ca.crt ~/crl.pem       install/update CA + CRL, listener 8884
#   sudo ./65-mtls.sh --crl ~/crl.pem          only a new CRL (after a revoke)
#   sudo ./65-mtls.sh --close-8883             all devices use mTLS: close the
#                                              password listener for the internet
#
# IEC 62443-4-2 CR 1.2 / CR 1.9: devices authenticate with a key that never
# leaves the device and cannot be replayed; the server stores no device
# secrets, and a single device can be revoked (CRL) without touching others.

# shellcheck source=common.sh
. "$(dirname "$0")/common.sh"
require_root

DIR=/etc/mosquitto/ca_certificates
CA_DST=$DIR/zephyr-a-device-ca.crt
CRL_DST=$DIR/zephyr-a-device-crl.pem

install_crl() { # <crl.pem> <ca.crt>
	openssl crl -in "$1" -noout -CAfile "$2" 2>&1 | grep -q "verify OK" ||
		die "$1 is not a CRL signed by $2"
	local next
	next=$(openssl crl -in "$1" -noout -nextupdate | cut -d= -f2)
	if [ "$(date -d "$next" +%s)" -lt "$(date -d '+30 days' +%s)" ]; then
		warn "CRL expires $next - after that EVERY device is refused (device_pki.py crl)"
	fi
	install -m 0644 -o mosquitto -g mosquitto "$1" "$CRL_DST"
	ok "CRL installed (next update $next, $(openssl crl -in "$1" -noout -text | grep -c 'Serial Number') revoked)"
}

restart_broker() {
	# A new listener / CA / CRL is only read at start (not on SIGHUP).
	systemctl restart mosquitto
	sleep 1
	systemctl is-active --quiet mosquitto || {
		journalctl -u mosquitto -n 20 --no-pager | sed 's/^/    /'
		die "mosquitto failed to start"
	}
	ok "mosquitto restarted (devices reconnect automatically)"
}

case "${1:-}" in
--crl)
	[ -f "${2:-}" ] || die "Usage: sudo $0 --crl <crl.pem>"
	[ -f "$CA_DST" ] || die "Run '$0 <ca.crt> <crl.pem>' first"
	info "Update CRL"
	install_crl "$2" "$CA_DST"
	restart_broker
	exit 0
	;;
--close-8883)
	info "Close the password listener 8883 for the internet"
	ufw delete allow 8883/tcp >/dev/null 2>&1 || true
	ok "8883/tcp no longer allowed (Mosquitto still listens; 8884 = mTLS only)"
	exit 0
	;;
esac

CA="${1:-}"
CRL="${2:-}"
if [ ! -f "$CA" ] || [ ! -f "$CRL" ]; then
	die "Usage: sudo $0 <ca.crt> <crl.pem>"
fi

info "Check the device CA"
openssl x509 -in "$CA" -noout -text | grep -q "CA:TRUE" || die "$CA is not a CA certificate"
openssl x509 -in "$CA" -noout -subject -enddate | sed 's/^/    /'
if openssl pkey -in "$CA" -noout 2>/dev/null; then
	die "$CA contains a private key - copy only ca.crt to the server"
fi

info "Install CA, CRL and listener 8884"
install -d -m 0755 "$DIR"
install -m 0644 -o mosquitto -g mosquitto "$CA" "$CA_DST"
install_crl "$CRL" "$CA_DST"
install -m 0644 "$CLOUD_DIR/mosquitto/zephyr_a_mtls.conf" /etc/mosquitto/conf.d/zephyr_a_mtls.conf
ufw allow 8884/tcp comment 'MQTT over TLS with client certificate (devices)' >/dev/null
ok "$CA_DST, /etc/mosquitto/conf.d/zephyr_a_mtls.conf, 8884/tcp allowed"
restart_broker
ss -ltn | grep -E ':8884\b' | awk '{print "    listening on " $4}'

info "Test: a client without certificate must be refused"
if echo | timeout 10 openssl s_client -connect "$DOMAIN:8884" -servername "$DOMAIN" 2>&1 |
	grep -qiE 'alert|certificate required|errno'; then
	ok "refused without client certificate"
else
	warn "no refusal seen - check 'sudo tail /var/log/mosquitto/mosquitto.log'"
fi

cat <<TXT

    Next on the PC: .\\scripts\\build.ps1 -Mtls -DeviceId <id>; .\\scripts\\flash.ps1
    The device then connects to port 8884 without a password.
TXT
