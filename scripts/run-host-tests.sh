#!/usr/bin/env bash
# Zephyr_A - run every host test in one go (part 11).
#
#   scripts/run-host-tests.sh <zephyr-workspace> [<firmware build dir>]
#
# Runs on Linux, WSL or the devcontainer (native_sim does not run on Windows).
# Steps that need a tool that is not installed are SKIPPED, not failed, and
# listed at the end:
#   1  app/tests/lp_supervisor  ztest, LP-core decision logic        (west)
#   2  app/tests/health         ztest, task watchdog                 (west)
#   3  app/tests/mqtt_tls       firmware against real Mosquitto      (west + MOSQ_DIR or mosquitto)
#   4  cloud/node-red/test      decode, ncmd, overview, dashcmd, flows (node)
#   5  scripts/device_pki.py    issue/revoke + openssl verify        (python3 + cryptography, openssl)
#   6  fail2ban filter          sample log                           (fail2ban-regex)
#   7  shellcheck               all bash scripts                     (shellcheck)
#   8  memory limits            scripts/memory_layout.py --check     (firmware build dir given)
#
# Exit code 1 if any step failed.

set -u
WS=${1:?usage: run-host-tests.sh <zephyr-workspace> [<firmware build dir>]}
FW_BUILD=${2:-}
REPO=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d /tmp/zephyr_a_host_tests.XXXX)
declare -a RESULTS=()
FAILED=0

result() { # <step> <PASS|FAIL|SKIP> <detail>
	RESULTS+=("$(printf '%-4s %-28s %s' "$2" "$1" "$3")")
	[ "$2" = FAIL ] && FAILED=1
	return 0
}

ztest() { # <name>
	if ! command -v west >/dev/null; then result "$1" SKIP "west not found"; return; fi
	local log=$TMP/$1.log
	(cd "$WS" && west build -p always -b native_sim/native/64 -d "$TMP/build-$1" \
		"$REPO/app/tests/$1" -t run) >"$log" 2>&1
	local line
	line=$(grep -E "^SUITE (PASS|FAIL)" "$log" | tail -1)
	if [[ "$line" == "SUITE PASS"* ]]; then result "$1" PASS "${line#*: }"
	else result "$1" FAIL "see $log"; fi
}

echo "== 1/2 ztest suites"
ztest lp_supervisor
ztest health

echo "== 3 mqtt_tls (native_sim + Mosquitto)"
if command -v west >/dev/null && { [ -n "${MOSQ_DIR:-}" ] || command -v mosquitto >/dev/null; }; then
	if bash "$REPO/app/tests/mqtt_tls/run.sh" "$WS" ${LE_CERTS:+"$LE_CERTS"} >"$TMP/mqtt_tls.log" 2>&1; then
		result mqtt_tls PASS "$(grep -E '^Result:' "$TMP/mqtt_tls.log" | tail -1)"
	else
		result mqtt_tls FAIL "$(grep -E '^Result:' "$TMP/mqtt_tls.log" | tail -1) - see $TMP/mqtt_tls.log"
	fi
else
	result mqtt_tls SKIP "needs west and mosquitto (or MOSQ_DIR)"
fi

echo "== 4 Node-RED functions"
if command -v node >/dev/null; then
	ok=1
	n=0
	for t in "$REPO"/cloud/node-red/test/*.test.js; do
		node "$t" >>"$TMP/node.log" 2>&1 || ok=0
		n=$((n + 1))
	done
	if [ $ok = 1 ]; then result node-red PASS "$n test files"
	else result node-red FAIL "see $TMP/node.log"; fi
else
	result node-red SKIP "node not found"
fi

echo "== 5 device PKI"
if python3 -c "import cryptography" 2>/dev/null && command -v openssl >/dev/null; then
	P="python3 $REPO/scripts/device_pki.py --dir $TMP/pki"
	if $P init >/dev/null && $P issue dev-a >/dev/null && $P issue dev-b >/dev/null && $P revoke dev-b >/dev/null &&
		openssl verify -CAfile "$TMP/pki/ca.crt" "$TMP/pki/devices/dev-a.crt" >/dev/null 2>&1 &&
		! openssl verify -crl_check -CRLfile "$TMP/pki/crl.pem" -CAfile "$TMP/pki/ca.crt" \
			"$TMP/pki/devices/dev-b.crt" >/dev/null 2>&1; then
		result device_pki PASS "chain OK, revoked certificate refused"
	else
		result device_pki FAIL "openssl verify mismatch"
	fi
else
	result device_pki SKIP "python3 cryptography or openssl missing"
fi

echo "== 6 fail2ban filter"
if command -v fail2ban-regex >/dev/null; then
	n=$(fail2ban-regex "$REPO/cloud/fail2ban/test/mosquitto-sample.log" \
		"$REPO/cloud/fail2ban/filter.d/zephyr-a-mosquitto.conf" | sed -n 's/^Lines: .* \([0-9]*\) matched.*/\1/p')
	if [ "$n" = 14 ]; then result fail2ban PASS "7 failures from 5 addresses"
	else result fail2ban FAIL "matched lines: ${n:-?} (expected 14)"; fi
else
	result fail2ban SKIP "fail2ban-regex not found"
fi

echo "== 7 shellcheck"
if command -v shellcheck >/dev/null; then
	mapfile -t files < <(cd "$REPO" && git ls-files '*.sh')
	if (cd "$REPO" && shellcheck -x -P SCRIPTDIR "${files[@]}"); then
		result shellcheck PASS "${#files[@]} scripts"
	else
		result shellcheck FAIL "see output above"
	fi
else
	result shellcheck SKIP "shellcheck not found"
fi

echo "== 8 memory limits"
if [ -n "$FW_BUILD" ]; then
	if python3 "$REPO/scripts/memory_layout.py" "$FW_BUILD" --check >"$TMP/memory.md" 2>"$TMP/memory.err"; then
		result memory PASS "$(cat "$TMP/memory.err")"
	else
		result memory FAIL "$(tr '\n' ' ' <"$TMP/memory.err")"
	fi
else
	result memory SKIP "no firmware build dir given"
fi

echo
echo "Summary (logs: $TMP)"
printf '  %s\n' "${RESULTS[@]}"
exit $FAILED
