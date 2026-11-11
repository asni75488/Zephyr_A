#!/usr/bin/env bash
# Zephyr_A - MQTT/TLS client test on native_sim against a real Mosquitto.
#
# Runs the firmware's mqtt_client.c (same TLS Kconfig as app/prj.conf) on the
# Native Simulator with host sockets, against Mosquitto 2.0 with the repo's
# ACL, and checks positive and negative cases (docs/06-mqtt-tls.md, 5.2).
#
# Requirements (Linux): Zephyr workspace with west, mosquitto 2.0 +
# mosquitto_sub/pub/passwd, openssl, python3, and a hosts entry
#   127.0.0.1 zephyra.vps.webdock.cloud
# so the TLS host name check uses the real broker name.
#
# Usage: run.sh <zephyr-workspace> [<letsencrypt cert dir for chain padding>]
#   env MOSQ_DIR=<mosquitto build dir with src/ client/ apps/ lib/>
set -euo pipefail

WS=$(realpath "$1")
PAD=${2:-}
HERE=$(cd "$(dirname "$0")" && pwd)
HOST=zephyra.vps.webdock.cloud
MOSQ_DIR=${MOSQ_DIR:-}
if [ -n "$MOSQ_DIR" ]; then
    MOSQUITTO=$MOSQ_DIR/src/mosquitto
    SUB=$MOSQ_DIR/client/mosquitto_sub
    PUB=$MOSQ_DIR/client/mosquitto_pub
    PASSWD=$MOSQ_DIR/apps/mosquitto_passwd/mosquitto_passwd
    export LD_LIBRARY_PATH=$MOSQ_DIR/lib
else
    MOSQUITTO=mosquitto; SUB=mosquitto_sub; PUB=mosquitto_pub; PASSWD=mosquitto_passwd
fi
W=$(mktemp -d /tmp/zephyr_a_mqtt_test.XXXX)
chmod 755 "$W" # the broker drops root privileges and must reach its files
PKI=$W/pki
BUILD=$W/build
EXE=$BUILD/zephyr/zephyr.exe
PASS=0
FAIL=0

getent hosts "$HOST" | grep -q 127.0.0.1 || { echo "Add '127.0.0.1 $HOST' to /etc/hosts"; exit 1; }

cleanup() { pkill -P $$ 2>/dev/null || true; }
trap cleanup EXIT

echo "== Test PKI"
bash "$HERE/pki.sh" "$PKI" "$HOST" "$PAD"
chmod 644 "$PKI"/*.key

# 4 trusted roots like the firmware (2 test roots + 2 real ISRG roots), so the
# Mbed TLS heap use is the same as on the device.
echo "== Build (native_sim/native/64)"
(cd "$WS" && west build -p always -b native_sim/native/64 "$HERE" -d "$BUILD" -- \
    "-DTEST_CA_DER=$PKI/ecdsa-root.der;$HERE/../../ca/isrg-root-yr.der;$HERE/../../ca/isrg-root-x2.der;$PKI/rsa-root.der" \
    -DCONFIG_APP_MQTT_CONNACK_TIMEOUT_S=5 >"$W/build.log" 2>&1) || { tail -30 "$W/build.log"; exit 1; }
if grep -E "warning:" "$W/build.log"; then echo "Compiler warnings"; exit 1; fi

echo "== Broker users (ACL from cloud/mosquitto/acl)"
: >"$W/passwd"
"$PASSWD" -b "$W/passwd" esp32c6-test0001 test-password-123 2>/dev/null
"$PASSWD" -b "$W/passwd" nodered nodered-pass-123 2>/dev/null
cp "$HERE/../../../cloud/mosquitto/acl" "$W/acl"
if id mosquitto >/dev/null 2>&1; then chown mosquitto: "$W/passwd" "$W/acl"; fi
chmod 600 "$W/passwd" "$W/acl"

broker_conf() { # <chain>
    cat <<EOF
per_listener_settings false
allow_anonymous false
password_file $W/passwd
acl_file $W/acl
connection_messages true
log_type error
log_type warning
log_type notice
log_type information
listener 1883 127.0.0.1
listener 8883
certfile $PKI/$1-fullchain.pem
keyfile $PKI/$1-leaf.key
tls_version tlsv1.2
EOF
}

start_broker() { # <chain> [plain]
    if [ "${2:-}" = plain ]; then
        printf 'allow_anonymous false\npassword_file %s\nlistener 8883\n' "$W/passwd" >"$W/broker.conf"
    else
        broker_conf "$1" >"$W/broker.conf"
    fi
    "$MOSQUITTO" -c "$W/broker.conf" >"$W/broker.log" 2>&1 &
    BROKER=$!
    sleep 1
}
stop_broker() { kill "$BROKER" 2>/dev/null || true; wait "$BROKER" 2>/dev/null || true; }

start_tls12_server() { # <chain>  (handshake only, no MQTT)
    openssl s_server -quiet -tls1_2 -accept 8883 -cert "$PKI/$1-leaf.pem" -key "$PKI/$1-leaf.key" \
        -cert_chain "$PKI/$1-fullchain.pem" >"$W/broker.log" 2>&1 &
    BROKER=$!
    sleep 1
}

run_device() { # <seconds> [extra zephyr.exe args]
    local t=$1
    shift
    timeout $((t + 30)) "$EXE" -run_s="$t" -stop_at=$((t + 5)) "$@" >"$W/dev.log" 2>&1 || true
}

check() { # <description> <grep pattern> [file]
    if grep -Eq "$2" "${3:-$W/dev.log}"; then
        echo "  PASS  $1"; PASS=$((PASS + 1))
    else
        echo "  FAIL  $1 (pattern: $2)"; FAIL=$((FAIL + 1))
    fi
}
check_not() {
    if grep -Eq "$2" "${3:-$W/dev.log}"; then
        echo "  FAIL  $1 (unexpected: $2)"; FAIL=$((FAIL + 1))
    else
        echo "  PASS  $1"; PASS=$((PASS + 1))
    fi
}
save_log() { cp "$W/dev.log" "$W/dev-$1.log"; cp "$W/broker.log" "$W/broker-$1.log" 2>/dev/null || true; }

echo "== 1. ECDSA chain (Let's Encrypt YE shape), TLS 1.3, data, NCMD, link loss"
start_broker ecdsa
"$SUB" -h 127.0.0.1 -p 1883 -u nodered -P nodered-pass-123 -t 'spBv1.0/#' -v -F '%t %x' >"$W/sub.log" 2>&1 &
SUBPID=$!
( sleep 6; "$PUB" -h 127.0.0.1 -p 1883 -u nodered -P nodered-pass-123 -q 1 \
    -t spBv1.0/ProjektA/NCMD/esp32c6-test0001 -m 'ncmd-test' ) &
run_device 20 -link_loss_at=12
kill $SUBPID 2>/dev/null || true
save_log ecdsa
check "TLS 1.3 handshake, certificate + host name verified" "TLS established .*TLS 1\.3"
check "CONNACK accepted" "MQTT connected"
check "Subscribed to own NCMD" "Subscribed spBv1.0/ProjektA/NCMD/esp32c6-test0001"
check "NCMD received by device" "RX spBv1.0/ProjektA/NCMD/esp32c6-test0001 \(9 B\)"
check "NDATA arrived at broker subscriber" "^spBv1.0/ProjektA/NDATA/esp32c6-test0001 " "$W/sub.log"
python3 "$HERE/decode_ndata.py" "$W/sub.log" >"$W/decoded.txt"
check "NDATA decodes as Sparkplug B (Test/Uptime, RSSI -71)" "Test/RSSI=-71.0" "$W/decoded.txt"
check "Link loss closes session without backoff" "Network link lost"
check "Reconnects after link loss" "connects=2 "
check "No publish dropped" "dropped=0 "
check_not "No errors logged" "<err>"
grep "TEST RESULT" "$W/dev.log" | sed 's/^/  /' || true
stop_broker

echo "== 2. RSA chain (Let's Encrypt YR shape), TLS 1.3"
start_broker rsa
run_device 6
save_log rsa
check "RSA chain verified" "TLS established .*TLS 1\.3"
check "CONNACK accepted" "MQTT connected"
grep "TEST RESULT" "$W/dev.log" | sed 's/^/  /' || true
stop_broker

echo "== 3. TLS 1.2 only server (fallback if TLS 1.3 were disabled on the broker)"
start_tls12_server ecdsa
run_device 4
save_log tls12-ecdsa
check "TLS 1.2 handshake with ECDSA certificate" "TLS established .*TLS 1\.2 ECDHE-ECDSA"
stop_broker
start_tls12_server rsa
run_device 4
save_log tls12-rsa
# Known Mbed TLS 4.1 limitation (docs/06-mqtt-tls.md): OpenSSL signs the TLS 1.2
# ServerKeyExchange with RSA-PSS, which the Mbed TLS 1.2 client refuses (-0x6600).
# Not relevant for the project broker, which negotiates TLS 1.3.
if grep -q "TLS established .*TLS 1\.2 ECDHE-RSA" "$W/dev.log"; then
    echo "  PASS  TLS 1.2 handshake with RSA certificate"; PASS=$((PASS + 1))
elif grep -q "TLS handshake error: -0x6600" "$W/dev.log"; then
    echo "  KNOWN TLS 1.2 + RSA certificate refused (-0x6600, RSA-PSS in TLS 1.2), see docs"
else
    echo "  FAIL  TLS 1.2 + RSA: unexpected result"; FAIL=$((FAIL + 1))
fi
stop_broker

echo "== 4. Negative: certificate from an untrusted CA"
start_broker rogue
run_device 4
save_log rogue
check "Handshake refused (X509 verify failed -0x2700)" "TLS handshake error: -0x2700"
check "Permanent error -> max backoff" "Reconnect in (8|9|10) s"
check_not "No MQTT session" "MQTT connected"
stop_broker

echo "== 5. Negative: trusted CA, wrong host name in certificate"
start_broker wrongname
run_device 4
save_log wrongname
check "Handshake refused (host name mismatch -0x2700)" "TLS handshake error: -0x2700"
check_not "No MQTT session" "MQTT connected"
stop_broker

echo "== 6. Negative: plain MQTT (no TLS) on 8883"
start_broker ecdsa plain
run_device 4
save_log plain
check "TLS handshake fails against plain MQTT" "TLS handshake error|mqtt_connect failed"
check_not "No MQTT session" "MQTT connected"
stop_broker

echo "== 7. Negative: wrong password"
start_broker ecdsa
"$PASSWD" -b "$W/passwd" esp32c6-test0001 another-password 2>/dev/null
kill -HUP "$BROKER"; sleep 1
run_device 4
save_log badpass
check "CONNACK refused" "CONNACK: (not authorised|bad user name or password)"
check "Permanent error -> max backoff" "Reconnect in (8|9|10) s"
"$PASSWD" -b "$W/passwd" esp32c6-test0001 test-password-123 2>/dev/null
stop_broker

echo "== 8. Broker restart -> reconnect with backoff"
start_broker ecdsa
( sleep 3; kill "$BROKER"; sleep 3; "$MOSQUITTO" -c "$W/broker.conf" >>"$W/broker.log" 2>&1 & ) &
run_device 16
save_log restart
check "Session end detected" "Session ended|Connection closed"
check "Reconnected" "connects=2 "
pkill -f "$W/broker.conf" 2>/dev/null || true

echo
echo "Result: $PASS passed, $FAIL failed. Logs: $W"
[ "$FAIL" -eq 0 ]
