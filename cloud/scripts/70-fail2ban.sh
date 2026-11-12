#!/usr/bin/env bash
# Zephyr_A - 70: fail2ban against password guessing on MQTT and SSH (part 10).
#
#   sudo ./70-fail2ban.sh                         install / update
#   sudo IGNORE_IP="198.51.100.0/24" ./70-fail2ban.sh
#                                                 never ban these addresses
#
# IEC 62443-4-2 CR 1.11 (unsuccessful login attempts): Mosquitto itself has
# no lock-out; fail2ban reads its log and blocks the source address in UFW
# after 10 failed logins / refused TLS handshakes within 10 minutes.
# Filter: cloud/fail2ban/filter.d/zephyr-a-mosquitto.conf (tested with
# fail2ban-regex against cloud/fail2ban/test/mosquitto-sample.log).

# shellcheck source=common.sh
. "$(dirname "$0")/common.sh"
require_root

IGNORE_IP="${IGNORE_IP:-}"
if [ -n "$IGNORE_IP" ] && ! [[ "$IGNORE_IP" =~ ^[0-9a-fA-F.:/\ ]+$ ]]; then
	die "IGNORE_IP may only contain addresses/networks separated by spaces"
fi

info "Install fail2ban"
if ! command -v fail2ban-client >/dev/null; then
	apt-get update -qq
	DEBIAN_FRONTEND=noninteractive apt-get install -y -qq fail2ban
fi
fail2ban-client --version | head -1 | sed 's/^/    /'

info "Mosquitto log file"
LOG=/var/log/mosquitto/mosquitto.log
grep -q "^log_dest file $LOG" /etc/mosquitto/mosquitto.conf ||
	warn "/etc/mosquitto/mosquitto.conf does not log to $LOG - check log_dest"
[ -f "$LOG" ] || { install -m 0640 -o mosquitto -g adm /dev/null "$LOG"; ok "created $LOG"; }

info "Filter and jails"
install -m 0644 "$CLOUD_DIR/fail2ban/filter.d/zephyr-a-mosquitto.conf" /etc/fail2ban/filter.d/
sed "s|@IGNORE_IP@|$IGNORE_IP|" "$CLOUD_DIR/fail2ban/jail.d/zephyr-a.local" \
	>/etc/fail2ban/jail.d/zephyr-a.local
chmod 0644 /etc/fail2ban/jail.d/zephyr-a.local
ok "/etc/fail2ban/filter.d/zephyr-a-mosquitto.conf, /etc/fail2ban/jail.d/zephyr-a.local"

info "Filter self-test (sample log, 7 failures from 5 addresses expected)"
n=$(fail2ban-regex "$CLOUD_DIR/fail2ban/test/mosquitto-sample.log" \
	/etc/fail2ban/filter.d/zephyr-a-mosquitto.conf | sed -n 's/^Lines: .* \([0-9]*\) matched.*/\1/p')
if [ "$n" = "14" ]; then
	ok "filter matches (14 lines = 7 failures)"
else
	die "filter self-test failed (matched lines: '${n:-?}')"
fi

info "Start"
systemctl enable fail2ban >/dev/null 2>&1
systemctl restart fail2ban
sleep 2
fail2ban-client status | sed 's/^/    /'
fail2ban-client status zephyr-a-mosquitto | sed 's/^/    /'

cat <<'TXT'

    Useful commands:
      sudo fail2ban-client status zephyr-a-mosquitto     banned addresses
      sudo fail2ban-client set zephyr-a-mosquitto unbanip <ip>
      sudo fail2ban-regex /var/log/mosquitto/mosquitto.log zephyr-a-mosquitto
TXT
