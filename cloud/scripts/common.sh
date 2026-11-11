#!/usr/bin/env bash
# Zephyr_A - shared helpers for the server setup scripts (sourced, not run).
# Target: Webdock VPS, Ubuntu 24.04 "Noble" LEMP image.
# shellcheck shell=bash

set -euo pipefail

# Settings shared by all scripts and the certbot deploy hook.
ENV_FILE=/etc/default/zephyr-a
if [ -r "$ENV_FILE" ]; then
	# shellcheck source=/dev/null
	. "$ENV_FILE"
fi

DOMAIN="${DOMAIN:-zephyra.vps.webdock.cloud}"
NR_USER="${NR_USER:-${SUDO_USER:-admin}}"
# Used by the scripts that source this file.
# shellcheck disable=SC2034
CLOUD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

info() { printf '\033[36m==> %s\033[0m\n' "$*"; }
ok() { printf '    \033[32m[ok]\033[0m %s\n' "$*"; }
warn() { printf '    \033[33m[warn]\033[0m %s\n' "$*"; }
die() {
	printf '\033[31m[error]\033[0m %s\n' "$*" >&2
	exit 1
}

require_root() {
	[ "$(id -u)" -eq 0 ] || die "Run this script with sudo, e.g.: sudo $0"
}

# Store KEY=VALUE in /etc/default/zephyr-a (idempotent).
save_env() {
	local key="$1" value="$2"
	touch "$ENV_FILE"
	chmod 0644 "$ENV_FILE"
	if grep -q "^${key}=" "$ENV_FILE"; then
		sed -i "s#^${key}=.*#${key}=\"${value}\"#" "$ENV_FILE"
	else
		printf '%s="%s"\n' "$key" "$value" >>"$ENV_FILE"
	fi
}

user_home() {
	getent passwd "$1" | cut -d: -f6
}

# Read a password twice without echo. Result in variable named by $1.
read_password() {
	local __var="$1" prompt="$2" p1 p2
	while true; do
		read -r -s -p "    $prompt: " p1
		echo
		read -r -s -p "    Repeat: " p2
		echo
		if [ -z "$p1" ]; then
			warn "Empty password, try again"
		elif [ "${#p1}" -lt 12 ]; then
			warn "Use at least 12 characters"
		elif [ "$p1" != "$p2" ]; then
			warn "Passwords differ, try again"
		else
			break
		fi
	done
	printf -v "$__var" '%s' "$p1"
}
