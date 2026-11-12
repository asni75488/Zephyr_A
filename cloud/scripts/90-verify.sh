#!/usr/bin/env bash
# Zephyr_A - 90: check the whole server setup.
#
#   sudo ./90-verify.sh
#
# Read-only checks: services, listening ports, firewall, certificate, TLS on
# 8883 as a device sees it, MQTT login/ACL behaviour, Node-RED HTTPS.

# shellcheck source=common.sh
. "$(dirname "$0")/common.sh"
require_root

FAIL=0
pass() { printf '    \033[32mPASS\033[0m %s\n' "$*"; }
fail() {
	printf '    \033[31mFAIL\033[0m %s\n' "$*"
	FAIL=$((FAIL + 1))
}
check() { # description, command...
	local desc="$1"
	shift
	if "$@" >/dev/null 2>&1; then pass "$desc"; else fail "$desc"; fi
}

info "Services"
check "mosquitto active" systemctl is-active --quiet mosquitto
check "nodered active" systemctl is-active --quiet nodered
check "nginx active" systemctl is-active --quiet nginx

info "Listening ports"
ss -ltn | awk 'NR>1 {print $4}' | grep -E ':(22|80|443|1880|1883|8883)$' | sort -u | sed 's/^/    /'
check "1883 bound to 127.0.0.1 only" sh -c "ss -ltn | grep -E ':1883\b' | grep -qv '127.0.0.1:1883' && exit 1 || exit 0"
check "8883 listening" sh -c "ss -ltn | grep -qE ':8883\b'"

info "Firewall"
check "UFW active" sh -c "ufw status | grep -q 'Status: active'"
if [ -f /etc/mosquitto/conf.d/zephyr_a_mtls.conf ]; then
	check "8884/tcp allowed (mTLS, part 10)" sh -c "ufw status | grep -qE '^8884/tcp +ALLOW'"
	check "8884 listening" sh -c "ss -ltn | grep -qE ':8884\b'"
	check "CRL valid for more than 30 days" sh -c "[ \$(date -d \"\$(openssl crl -in /etc/mosquitto/ca_certificates/zephyr-a-device-crl.pem -noout -nextupdate | cut -d= -f2)\" +%s) -gt \$(date -d '+30 days' +%s) ]"
	check "8884 refuses a client without certificate" sh -c "echo | timeout 10 openssl s_client -connect $DOMAIN:8884 -servername $DOMAIN 2>&1 | grep -qiE 'alert|certificate required'"
	ufw status | grep -qE '^8883/tcp +ALLOW' && warn "8883 (password) still open - close it with 65-mtls.sh --close-8883 when all devices use mTLS"
else
	check "8883/tcp allowed" sh -c "ufw status | grep -qE '^8883/tcp +ALLOW'"
fi
if command -v fail2ban-client >/dev/null; then
	check "fail2ban jail zephyr-a-mosquitto active (part 10)" sh -c "fail2ban-client status zephyr-a-mosquitto >/dev/null"
fi
check "1883 not allowed from outside" sh -c "! ufw status | grep -qE '^1883'"
check "FTP port 21 closed" sh -c "! ufw status | grep -qE '^21(/tcp)? +ALLOW'"

info "Certificate"
if [ -n "${CERT_DIR:-}" ] && [ -f "$CERT_DIR/cert.pem" ]; then
	end=$(openssl x509 -in "$CERT_DIR/cert.pem" -noout -enddate | cut -d= -f2)
	days=$((($(date -d "$end" +%s) - $(date +%s)) / 86400))
	echo "    $CERT_DIR, expires $end ($days days)"
	check "certificate valid > 7 days" test "$days" -gt 7
	check "Mosquitto has the current certificate" cmp -s "$CERT_DIR/fullchain.pem" /etc/mosquitto/certs/fullchain.pem
else
	fail "CERT_DIR not set or missing"
fi
check "certbot deploy hook installed" test -x /etc/letsencrypt/renewal-hooks/deploy/zephyr-a.sh

info "TLS on $DOMAIN:8883 (as a device sees it: system CA store + host name)"
tls_out=$(echo | timeout 10 openssl s_client -connect "$DOMAIN:8883" -servername "$DOMAIN" \
	-verify_hostname "$DOMAIN" -verify_return_error 2>&1 || true)
echo "$tls_out" | grep -E "Protocol *:|Cipher *:|Verify return code" | sort -u | sed 's/^/    /'
check "certificate chain + host name verified" sh -c "echo \"\$0\" | grep -q 'Verify return code: 0 (ok)'" "$tls_out"
check "TLS 1.1 refused" sh -c "echo | timeout 10 openssl s_client -connect $DOMAIN:8883 -tls1_1 -cipher 'DEFAULT@SECLEVEL=0' 2>&1 | grep -qiE 'alert|no protocols|wrong version'"

info "MQTT access control"
check "anonymous connection refused" sh -c "! timeout 10 mosquitto_pub -h $DOMAIN -p 8883 --capath /etc/ssl/certs -t test -m x"
check "plain MQTT on 8883 refused" sh -c "! timeout 10 mosquitto_pub -h 127.0.0.1 -p 8883 -t test -m x"
echo "    Users in /etc/mosquitto/passwd: $(cut -d: -f1 /etc/mosquitto/passwd | tr '\n' ' ')"

info "Node-RED"
code=$(curl -s -o /dev/null -w '%{http_code}' "https://$DOMAIN:1880/" || true)
check "editor reachable over HTTPS with valid certificate (HTTP $code)" test "$code" = "200"
code=$(curl -s -o /dev/null -w '%{http_code}' "https://$DOMAIN:1880/dashboard/" || true)
check "dashboard requires login (HTTP $code = 401)" test "$code" = "401"
check "flows_cred.json encrypted (no plain password)" sh -c "! grep -q '\"password\"' $(user_home "$NR_USER")/.node-red/flows_cred.json 2>/dev/null"

echo
if [ "$FAIL" -eq 0 ]; then
	info "All checks passed"
else
	printf '\033[31m==> %d check(s) failed\033[0m\n' "$FAIL"
	exit 1
fi
