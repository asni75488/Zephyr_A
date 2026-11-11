#!/usr/bin/env bash
# Zephyr_A - 20: firewall (UFW) on the Webdock VPS.
#
# Webdock stacks come with UFW enabled and ports 21, 22, 80, 443 and
# 50000-50099 (FTP) open. This script:
#   - keeps   22/tcp  SSH
#   - keeps   80/tcp  HTTP (Let's Encrypt HTTP-01 validation + redirect to HTTPS)
#   - keeps  443/tcp  HTTPS (nginx)
#   - opens 8883/tcp  MQTT over TLS (devices)
#   - opens 1880/tcp  Node-RED editor + dashboard over HTTPS (login required)
#   - closes FTP (21/tcp, 50000-50099/tcp) and stops the FTP service:
#     plain-text protocol, credentials were exposed, not needed (use SFTP/scp)
#   - 1883 (plain MQTT) is NOT opened: Mosquitto binds it to 127.0.0.1 only
#
#   sudo ./20-firewall.sh             apply
#   sudo ./20-firewall.sh --keep-ftp  do not touch FTP
#
# Keep your current SSH session open until you have tested a NEW login.

# shellcheck source=common.sh
. "$(dirname "$0")/common.sh"
require_root

KEEP_FTP=0
[ "${1:-}" = "--keep-ftp" ] && KEEP_FTP=1

command -v ufw >/dev/null || apt-get install -y -q ufw

info "UFW rules"
ufw allow 22/tcp comment 'SSH' >/dev/null
ufw allow 80/tcp comment 'HTTP (Lets Encrypt)' >/dev/null
ufw allow 443/tcp comment 'HTTPS' >/dev/null
ufw allow 8883/tcp comment 'MQTT over TLS (devices)' >/dev/null
ufw allow 1880/tcp comment 'Node-RED HTTPS (login)' >/dev/null
ok "22, 80, 443, 8883, 1880 allowed"

if [ "$KEEP_FTP" -eq 0 ]; then
	for rule in 21 21/tcp 50000:50099/tcp; do
		ufw --force delete allow "$rule" >/dev/null 2>&1 || true
	done
	for svc in pure-ftpd proftpd vsftpd; do
		if systemctl list-unit-files "${svc}.service" >/dev/null 2>&1 &&
			systemctl list-unit-files "${svc}.service" | grep -q "^${svc}.service"; then
			systemctl disable --now "$svc" >/dev/null 2>&1 || true
			ok "FTP service $svc stopped and disabled"
		fi
	done
	ok "FTP ports closed"
else
	warn "FTP left unchanged (--keep-ftp)"
fi

if ! ufw status | grep -q "Status: active"; then
	ufw --force enable >/dev/null
	ok "UFW enabled"
fi

info "Current rules"
ufw status verbose | sed 's/^/    /'

info "Done. Next: sudo ./30-certs.sh"
