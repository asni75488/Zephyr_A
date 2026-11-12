#!/usr/bin/env bash
# Zephyr_A - end-to-end test on native_sim against a real Mosquitto.
#
# Runs the firmware's app_logic, sparkplug (nanopb), mqtt_client, tls_ca and
# timesync (same TLS Kconfig as app/prj.conf) on the Native Simulator with
# host sockets, against Mosquitto 2.0 with the repo's ACL and a local SNTP
# server, and checks positive and negative cases (docs/06-mqtt-tls.md 5.2,
# docs/07-sparkplug-tid.md 6). check_e2e.py validates the Sparkplug session.
#
# Requirements (Linux): Zephyr workspace with west, mosquitto 2.0 +
# mosquitto_sub/pub/passwd, openssl, python3 (no packages), and a hosts entry
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
BUILDI=$W/build-interval
BUILDM=$W/build-mtls
BUILDO=$W/build-mtls-other
DEVPKI=$W/devpki
EXE_CONT=$EXE
EXE_INT=$BUILDI/zephyr/zephyr.exe
PASS=0
FAIL=0

getent ahostsv4 "$HOST" | grep -q "^127.0.0.1 " || { echo "Add '127.0.0.1 $HOST' to /etc/hosts"; exit 1; }

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
echo "== Build interval-uplink variant (part 9, prj-interval.conf)"
(cd "$WS" && west build -p always -b native_sim/native/64 "$HERE" -d "$BUILDI" -- \
    "-DTEST_CA_DER=$PKI/ecdsa-root.der;$HERE/../../ca/isrg-root-yr.der;$HERE/../../ca/isrg-root-x2.der;$PKI/rsa-root.der" \
    -DCONFIG_APP_MQTT_CONNACK_TIMEOUT_S=5 "-DEXTRA_CONF_FILE=$HERE/prj-interval.conf" \
    >"$W/build-interval.log" 2>&1) || { tail -30 "$W/build-interval.log"; exit 1; }
if grep -E "warning:" "$W/build-interval.log"; then echo "Compiler warnings"; exit 1; fi
echo "== Device PKI (scripts/device_pki.py) + mTLS builds (part 10)"
DPKI="python3 $HERE/../../../scripts/device_pki.py --dir $DEVPKI"
$DPKI init >/dev/null && $DPKI issue esp32c6-test0001 >/dev/null && $DPKI issue esp32c6-other >/dev/null
for v in test0001 other; do
    if [ $v = test0001 ]; then BD=$BUILDM; else BD=$BUILDO; fi
    (cd "$WS" && west build -p always -b native_sim/native/64 "$HERE" -d "$BD" -- \
        "-DTEST_CA_DER=$PKI/ecdsa-root.der;$HERE/../../ca/isrg-root-yr.der;$HERE/../../ca/isrg-root-x2.der;$PKI/rsa-root.der" \
        -DCONFIG_APP_MQTT_CONNACK_TIMEOUT_S=5 -DCONFIG_APP_MQTT_MTLS=y \
        "-DCONFIG_APP_MQTT_CLIENT_CERT_FILE=\"$DEVPKI/devices/esp32c6-$v.crt.der\"" \
        "-DCONFIG_APP_MQTT_CLIENT_KEY_FILE=\"$DEVPKI/devices/esp32c6-$v.key.der\"" \
        >"$W/build-mtls-$v.log" 2>&1) || { tail -30 "$W/build-mtls-$v.log"; exit 1; }
    if grep -E "warning:" "$W/build-mtls-$v.log"; then echo "Compiler warnings"; exit 1; fi
done

echo "== Broker users (ACL from cloud/mosquitto/acl)"
: >"$W/passwd"
"$PASSWD" -b "$W/passwd" esp32c6-test0001 test-password-123 2>/dev/null
"$PASSWD" -b "$W/passwd" nodered nodered-pass-123 2>/dev/null
cp "$HERE/../../../cloud/mosquitto/acl" "$W/acl"
if id mosquitto >/dev/null 2>&1; then chown mosquitto: "$W/passwd" "$W/acl"; fi
chmod 600 "$W/passwd" "$W/acl"

echo "== SNTP test server (127.0.0.1:1123)"
python3 "$HERE/sntp_server.py" 1123 &
SNTP=$!
sleep 0.5

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
listener ${BROKER_PORT:-8883}
certfile $PKI/$1-fullchain.pem
keyfile $PKI/$1-leaf.key
tls_version tlsv1.2
EOF
    if [ -n "${CERT_ON_8883:-}" ]; then # part 10: password listener now needs a certificate
        printf 'cafile %s\nrequire_certificate true\nuse_identity_as_username true\n' "$DEVPKI/ca.crt"
    fi
    if [ -n "${MTLS:-}" ]; then # part 10: like cloud/mosquitto/zephyr_a_mtls.conf
        cat <<EOF
listener ${MTLS_PORT:-8884}
certfile $PKI/$1-fullchain.pem
keyfile $PKI/$1-leaf.key
tls_version tlsv1.2
cafile $DEVPKI/ca.crt
crlfile $DEVPKI/crl.pem
require_certificate true
use_identity_as_username true
EOF
    fi
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

ncmd() { # <metric=type:value>... -> NCMD to the test device (as user nodered)
    python3 "$HERE/spb.py" ncmd "$@" | "$PUB" -h 127.0.0.1 -p 1883 -u nodered \
        -P nodered-pass-123 -t spBv1.0/ProjektA/NCMD/esp32c6-test0001 -s
}

echo "== 1. ECDSA chain, TLS 1.3, Sparkplug B session: BIRTH, DDATA, NCMD, rebirth, link loss"
start_broker ecdsa
"$SUB" -h 127.0.0.1 -p 1883 -u nodered -P nodered-pass-123 -t 'spBv1.0/#' -v -F '%t %x' >"$W/sub.log" 2>&1 &
SUBPID=$!
(
    sleep 5; ncmd "Config/Sample Interval=Int64:5000" "#6=Int64:4" "Foo/Bar=Int64:1" \
        "Node Control/Sleep=Int64:120"
    sleep 5; ncmd "Node Control/Rebirth=Boolean:true"
    sleep 2; python3 "$HERE/spb.py" ncmd "Foo=Int64:1" | "$PUB" -h 127.0.0.1 -p 1883 -u nodered \
        -P nodered-pass-123 -t spBv1.0/ProjektA/DCMD/esp32c6-test0001/sensors -s
) &
run_device 26 -link_loss_at=18
sleep 1
kill $SUBPID 2>/dev/null || true
save_log ecdsa
cp "$W/sub.log" "$W/sub-ecdsa.log"
check "Time synced from SNTP before connecting" "Time synced via 127.0.0.1:1123"
check "TLS 1.3, certificate (incl. dates) + host name verified" "TLS established .*TLS 1\.3"
check "CONNACK accepted" "MQTT connected"
check "Last Will = NDEATH" "Last Will: spBv1.0/ProjektA/NDEATH/esp32c6-test0001"
check "NCMD sample interval applied by app_logic" "Sample interval set to 5000 ms"
check "NCMD batch size by alias applied" "Batch size set to 4"
check "Unknown NCMD metric reported as security event" "SECURITY: NCMD with unsupported metric Foo/Bar"
check "DCMD ignored (no writable device metrics)" "SECURITY: command on spBv1.0/ProjektA/DCMD/.* ignored"
check "Rebirth executed" "NCMD: Node Control/Rebirth"
check "NCMD Sleep rejected without LP core (native_sim)" "SECURITY: NCMD Sleep rejected"
check "Link loss closes session without backoff" "Network link lost"
check "Reconnects after link loss" "connects=2 "
check_not "No errors logged" "<err>"
check_not "Broker denied nothing (ACL)" "[Dd]enied" "$W/broker.log"
python3 "$HERE/check_e2e.py" "$W/sub.log" | tee "$W/e2e.txt" || true
PASS=$((PASS + $(grep -c "  PASS" "$W/e2e.txt" || true)))
FAIL=$((FAIL + $(grep -c "  FAIL" "$W/e2e.txt" || true)))
grep "TEST RESULT" "$W/dev.log" | sed 's/^/  /' || true
stop_broker

echo "== 1b. NCMD Node Control/Reboot: rejected right after boot, then NDEATH + reboot"
start_broker ecdsa
"$SUB" -h 127.0.0.1 -p 1883 -u nodered -P nodered-pass-123 -t 'spBv1.0/#' -v -F '%t %x' >"$W/sub.log" 2>&1 &
SUBPID=$!
( sleep 2; ncmd "Node Control/Reboot=Boolean:true"; sleep 5; ncmd "Node Control/Reboot=Boolean:true" ) &
run_device 14
sleep 1
kill $SUBPID 2>/dev/null || true
save_log reboot
check "Reboot during the first 5 s rejected (reboot-loop protection)" "SECURITY: NCMD Reboot rejected - uptime"
check "Reboot accepted later" "SECURITY: NCMD Reboot accepted"
check "Reboot announced" "Rebooting in 2 s"
check_not "Device did not run to the end" "TEST RESULT"
python3 "$HERE/spb.py" decode "$W/sub.log" >"$W/reboot.json"
check "NDEATH published before the reboot" '"type": "NDEATH"' "$W/reboot.json"
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
sleep 1 # let the restarted broker free its ports

echo "== 9. Negative: expired and not yet valid server certificate (date check)"
for c in expired notyet; do
    start_broker $c
    run_device 4
    save_log $c
    check "$c certificate refused (-0x2700)" "TLS handshake error: -0x2700"
    check_not "$c: no MQTT session" "MQTT connected"
    stop_broker
done

echo "== 10. Negative: SNTP answers with a time before 2026 -> no network"
kill "$SNTP" 2>/dev/null || true; wait "$SNTP" 2>/dev/null || true
python3 "$HERE/sntp_server.py" 1123 -1000000000 &
SNTP=$!
sleep 0.5
start_broker ecdsa
run_device 4
save_log badtime
check "Implausible time rejected" "implausible time .* rejected"
check "Network not reported up without time" "no time .* network stays down"
check_not "No MQTT session" "MQTT connected"
stop_broker
kill "$SNTP" 2>/dev/null || true

echo "== 12. Interval uplink (part 9): link only for uplinks, NDEATH + clean DISCONNECT, historical data"
python3 "$HERE/sntp_server.py" 1123 &
SNTP=$!
sleep 0.5
start_broker ecdsa
"$SUB" -h 127.0.0.1 -p 1883 -u nodered -P nodered-pass-123 -t 'spBv1.0/#' -v -F '%t %x' >"$W/sub.log" 2>&1 &
SUBPID=$!
EXE=$EXE_INT
run_device 34 # ends between uplink #2 and #3
EXE=$EXE_CONT
sleep 1
kill $SUBPID 2>/dev/null || true
save_log interval
cp "$W/sub.log" "$W/sub-interval.log"
check "First uplink right after boot" "Uplink #1 ok"
check "Session ended with NDEATH" "NDEATH bdSeq [0-9]+ \\(planned\\)"
check "Clean MQTT DISCONNECT" "MQTT DISCONNECT \\(planned\\)"
check "Modem switched off after the uplink" "TEST: network down"
check "Early uplink when the batch queue is 3/4 full" "Uplink #2 early: batch queue"
check "Second uplink completed" "Uplink #2 ok"
check "No reconnect while the link is down (only uplink sessions)" "connects=2 .*uplinks=2"
check "Broker saw clean disconnects (no Will)" "esp32c6-test0001 disconnected\\." "$W/broker.log"
check_not "Broker never closed a session on its own" "esp32c6-test0001 closed its connection|exceeded timeout" "$W/broker.log"
check_not "No errors logged" "<err>"
python3 - "$W/sub-interval.log" "$HERE" <<'PY' | tee "$W/interval.txt"
import sys
sys.path.insert(0, sys.argv[2])
import spb
msgs = []
for line in open(sys.argv[1], encoding="ascii"):
    parts = line.split()
    if len(parts) == 2 and "/NCMD/" not in parts[0]:
        p = spb.decode_payload(bytes.fromhex(parts[1]))
        p["type"] = parts[0].split("/")[2]
        msgs.append(p)
def bdseq(m):
    return next((x.get("value") for x in m["metrics"] if x.get("name") == "bdSeq"), None)
births = [bdseq(m) for m in msgs if m["type"] == "NBIRTH"]
deaths = [bdseq(m) for m in msgs if m["type"] == "NDEATH"]
def check(desc, ok, detail=""):
    print(("  PASS  " if ok else "  FAIL  ") + desc + ("" if ok else f" ({detail})"))
check("One NDEATH per session with the bdSeq of its NBIRTH", births == deaths and len(births) >= 2,
      f"births {births} deaths {deaths}")
hist = [x for m in msgs if m["type"] == "DDATA" for x in m["metrics"] if x.get("is_historical")]
check("Data measured between uplinks marked is_historical", len(hist) > 0, len(hist))
ts = [x["timestamp"] for m in msgs if m["type"] == "DDATA" for x in m["metrics"]]
check("All DDATA values delivered in time order", ts == sorted(ts) and len(ts) >= 15, len(ts))
PY
PASS=$((PASS + $(grep -c "  PASS" "$W/interval.txt" || true)))
FAIL=$((FAIL + $(grep -c "  FAIL" "$W/interval.txt" || true)))
grep "TEST RESULT" "$W/dev.log" | sed 's/^/  /' || true
grep -E "Uplink #[0-9]+ (ok|timeout)" "$W/dev.log" | sed 's/^.*Uplink/  Uplink/' || true
stop_broker

echo "== 13. Data on the wire (TLS bytes via counting proxy), part 9"
BROKER_PORT=18883 start_broker ecdsa
python3 "$HERE/byteproxy.py" 8883 18883 "$W/bytes-cont.txt" &
PROXY=$!
sleep 0.5
run_device 33
sleep 1
kill $PROXY 2>/dev/null || true; wait $PROXY 2>/dev/null || true
save_log bytes-cont
python3 "$HERE/byteproxy.py" 8883 18883 "$W/bytes-int.txt" &
PROXY=$!
sleep 0.5
EXE=$EXE_INT
run_device 34
EXE=$EXE_CONT
sleep 1
kill $PROXY 2>/dev/null || true; wait $PROXY 2>/dev/null || true
save_log bytes-int
stop_broker
kill "$SNTP" 2>/dev/null || true
echo "  continuous (1 session, 33 s, 11 DDATA):"; sed 's/^/    /' "$W/bytes-cont.txt" 2>/dev/null || true
echo "  interval (one line per uplink):"; sed 's/^/    /' "$W/bytes-int.txt" 2>/dev/null || true
check "Byte counts recorded" "^conn 1 up [0-9]+ down [0-9]+" "$W/bytes-int.txt"

echo "== 14. mTLS (part 10): client certificate instead of password"
python3 "$HERE/sntp_server.py" 1123 &
SNTP=$!
MTLS=1 MTLS_PORT=18884 start_broker ecdsa
python3 "$HERE/byteproxy.py" 8884 18884 "$W/bytes-mtls.txt" &
PROXY=$!
"$SUB" -h 127.0.0.1 -p 1883 -u nodered -P nodered-pass-123 -t 'spBv1.0/#' -v -F '%t %x' >"$W/sub.log" 2>&1 &
SUBPID=$!
sleep 0.5
EXE=$BUILDM/zephyr/zephyr.exe
run_device 12
sleep 1
kill $SUBPID $PROXY 2>/dev/null || true
wait $PROXY 2>/dev/null || true
save_log mtls
check "Client certificate registered" "Client certificate [0-9]+ B \\+ key"
check "CN matches the device id" "mTLS: client certificate CN=esp32c6-test0001, port 8884"
check "Connected with the certificate" "MQTT connected \\(CONNACK"
check "Broker took the user name from the certificate CN" "as esp32c6-test0001 .*u'esp32c6-test0001'" "$W/broker.log"
check "ACL by CN: NBIRTH delivered" "spBv1.0/ProjektA/NBIRTH/esp32c6-test0001 " "$W/sub.log"
check "Data delivered" "spBv1.0/ProjektA/DDATA/esp32c6-test0001/" "$W/sub.log"
check_not "No errors logged" "<err>"
echo "  mTLS session bytes:"; sed 's/^/    /' "$W/bytes-mtls.txt" 2>/dev/null || true
stop_broker

echo "  -- revoked certificate"
$DPKI revoke esp32c6-test0001 >/dev/null
MTLS=1 start_broker ecdsa
run_device 6
save_log mtls-revoked
check "Revoked certificate refused by the broker (CRL)" "certificate verify failed|certificate revoked" "$W/broker.log"
check "Refusal logged as security event" "SECURITY: broker closed the connection after the TLS handshake.*refused"
check_not "No session with a revoked certificate" "MQTT connected \\(CONNACK"
stop_broker

echo "  -- certificate of another device"
EXE=$BUILDO/zephyr/zephyr.exe
MTLS=1 start_broker ecdsa
run_device 4
save_log mtls-other
check "Wrong certificate detected before connecting" "SECURITY: client certificate is for 'esp32c6-other', this device is 'esp32c6-test0001'"
check_not "No connection attempt" "New connection from" "$W/broker.log"
stop_broker

echo "  -- password device when the listener requires a certificate"
EXE=$EXE_CONT
CERT_ON_8883=1 start_broker ecdsa
run_device 6
save_log mtls-nocert
check "Device without certificate refused" "peer did not return a certificate|certificate required" "$W/broker.log"
check "Refusal logged as security event" "SECURITY: broker closed the connection after the TLS handshake.*required"
stop_broker
kill "$SNTP" 2>/dev/null || true

echo "== 11. No secrets in any device log"
if grep -l "test-password-123" "$W"/dev-*.log >/dev/null 2>&1; then
    echo "  FAIL  MQTT password found in a device log"; FAIL=$((FAIL + 1))
else
    echo "  PASS  MQTT password never logged ($(find "$W" -name "dev-*.log" | wc -l) logs checked)"; PASS=$((PASS + 1))
fi
if grep -l "BEGIN .*PRIVATE KEY" "$W"/dev-*.log >/dev/null 2>&1; then
    echo "  FAIL  private key material in a device log"; FAIL=$((FAIL + 1))
else
    echo "  PASS  no private key material in device logs"; PASS=$((PASS + 1))
fi
check "Security posture logged at boot" "SECURITY: no eFuses on this target|SECURITY: development device" "$W/dev-ecdsa.log"
check "Bad password logged as security event" "SECURITY: CONNACK" "$W/dev-badpass.log"
check "Untrusted certificate logged as security event" "SECURITY: TLS handshake failed" "$W/dev-rogue.log"

echo
echo "Result: $PASS passed, $FAIL failed. Logs: $W"
[ "$FAIL" -eq 0 ]
