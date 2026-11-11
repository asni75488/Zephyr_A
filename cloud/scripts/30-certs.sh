#!/usr/bin/env bash
# Zephyr_A - 30: Let's Encrypt certificate for MQTT/TLS and Node-RED.
#
# Webdock issues a Let's Encrypt certificate for the server's domain when the
# server is created (certbot + systemd timer for renewal). This script
#   1. finds that certificate (or requests one with certbot if missing),
#   2. installs a certbot deploy hook that copies it to Mosquitto/Node-RED
#      after every renewal,
#   3. prints the certificate chain (needed for the device's CA in part 6).
#
#   sudo ./30-certs.sh
#   sudo LE_EMAIL=you@example.com ./30-certs.sh   (only if a new certbot account is needed)

# shellcheck source=common.sh
. "$(dirname "$0")/common.sh"
require_root

command -v certbot >/dev/null || die "certbot not found (Webdock LEMP has it). Install: apt install certbot python3-certbot-nginx"

find_cert_dir() {
	local d
	for d in /etc/letsencrypt/live/*/; do
		[ -f "${d}cert.pem" ] || continue
		if openssl x509 -in "${d}cert.pem" -noout -ext subjectAltName 2>/dev/null |
			grep -q "DNS:${DOMAIN}\b"; then
			echo "${d%/}"
			return 0
		fi
	done
	return 1
}

info "Let's Encrypt certificate for $DOMAIN"
if ! CERT_DIR="$(find_cert_dir)"; then
	warn "No certificate found - requesting one with certbot (nginx plugin, nginx config is not changed)"
	email_args=(--register-unsafely-without-email)
	[ -n "${LE_EMAIL:-}" ] && email_args=(-m "$LE_EMAIL")
	certbot certonly --nginx -d "$DOMAIN" --non-interactive --agree-tos "${email_args[@]}"
	CERT_DIR="$(find_cert_dir)" || die "certbot did not produce a certificate for $DOMAIN"
fi
save_env CERT_DIR "$CERT_DIR"
ok "$CERT_DIR"

info "Certificate details"
openssl x509 -in "$CERT_DIR/cert.pem" -noout -subject -issuer -enddate | sed 's/^/    /'
key_alg=$(openssl x509 -in "$CERT_DIR/cert.pem" -noout -text | grep -m1 "Public Key Algorithm" | sed 's/.*: //')
echo "    key: $key_alg"
echo "    chain (intermediate -> root used by the device in part 6):"
openssl crl2pkcs7 -nocrl -certfile "$CERT_DIR/chain.pem" | openssl pkcs7 -print_certs -noout |
	grep -E "subject|issuer" | sed 's/^/      /'

info "Renewal timer"
if systemctl list-timers --all 2>/dev/null | grep -qE "certbot|snap.certbot"; then
	systemctl list-timers --all | grep -E "certbot" | sed 's/^/    /'
else
	warn "No certbot timer found - check: certbot renew --dry-run"
fi

info "Deploy hook"
install -d -m 0755 /etc/letsencrypt/renewal-hooks/deploy
install -m 0755 "$CLOUD_DIR/letsencrypt/zephyr-a-deploy-hook.sh" /etc/letsencrypt/renewal-hooks/deploy/zephyr-a.sh
RENEWED_LINEAGE="$CERT_DIR" /etc/letsencrypt/renewal-hooks/deploy/zephyr-a.sh
ok "installed and run once (/etc/letsencrypt/renewal-hooks/deploy/zephyr-a.sh)"

info "Done. Next: sudo ./40-mosquitto.sh"
