#!/usr/bin/env bash
# Zephyr_A - 10: base packages and settings on the Webdock VPS.
#
#   sudo ./10-base.sh            install packages, enable automatic security updates
#   sudo ./10-base.sh --upgrade  also run a full "apt upgrade"
#
# Idempotent: safe to run again.

# shellcheck source=common.sh
. "$(dirname "$0")/common.sh"
require_root

UPGRADE=0
[ "${1:-}" = "--upgrade" ] && UPGRADE=1

info "Settings (stored in $ENV_FILE)"
save_env DOMAIN "$DOMAIN"
save_env NR_USER "$NR_USER"
ok "DOMAIN=$DOMAIN NR_USER=$NR_USER"

info "Packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
if [ "$UPGRADE" -eq 1 ]; then
	apt-get -y upgrade
fi
apt-get install -y -q mosquitto mosquitto-clients openssl ca-certificates curl git \
	unattended-upgrades
ok "mosquitto $(dpkg-query -W -f='${Version}' mosquitto), openssl $(openssl version | cut -d' ' -f2)"

info "Automatic security updates (unattended-upgrades)"
cat >/etc/apt/apt.conf.d/20auto-upgrades <<'EOF'
APT::Periodic::Update-Package-Lists "1";
APT::Periodic::Unattended-Upgrade "1";
EOF
systemctl enable --now unattended-upgrades >/dev/null 2>&1 || true
ok "enabled"

info "Time synchronisation (needed for TLS certificate checks and logs)"
if timedatectl show -p NTPSynchronized --value 2>/dev/null | grep -q yes; then
	ok "NTP synchronised ($(timedatectl show -p Timezone --value))"
else
	warn "NTP not synchronised yet: $(timedatectl show -p NTP --value 2>/dev/null || echo unknown)"
fi

info "Done. Next: sudo ./20-firewall.sh"
