#!/usr/bin/env bash
# Zephyr_A - test PKI for app/tests/mqtt_tls (never used on the real server).
#
# Builds certificate chains with the same shape and sizes as Let's Encrypt
# "Generation Y" (docs/06-mqtt-tls.md):
#   ecdsa:  leaf P-256 <- intermediate P-384 <- root P-384 (+2 cross certs)
#   rsa:    leaf RSA-2048 <- intermediate RSA-2048 <- root RSA-4096 (+1 cross)
#   rogue:  same shape, from a root the device does NOT trust
#   wrongname: trusted ECDSA chain, but the leaf is for another host name
# Usage: pki.sh <out-dir> <host-name> [<letsencrypt cert dir for padding>]
set -euo pipefail

OUT=$1
HOST=$2
PAD=${3:-}
mkdir -p "$OUT"
cd "$OUT"

ext_int() { printf 'basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,digitalSignature,keyCertSign,cRLSign\nsubjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid\n'; }
ext_leaf() { printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature%s\nextendedKeyUsage=serverAuth\nsubjectAltName=DNS:%s\nsubjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid\n' "$2" "$1"; }

key() { # key <file> ecdsa256|ecdsa384|rsa2048|rsa4096
    case $2 in
        ecdsa256) openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$1" ;;
        ecdsa384) openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-384 -out "$1" ;;
        rsa2048) openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$1" ;;
        rsa4096) openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:4096 -out "$1" ;;
    esac 2>/dev/null
}

chain() { # chain <name> <rootkey> <intkey> <leafkey> <leaf-host> <digest> <leaf-ku-extra>
    local n=$1
    key "$n-root.key" "$2"; key "$n-int.key" "$3"; key "$n-leaf.key" "$4"
    openssl req -x509 -new -key "$n-root.key" -subj "/C=DK/O=Zephyr_A Test/CN=Test Root $n" \
        -days 3650 -"$6" -addext basicConstraints=critical,CA:TRUE \
        -addext keyUsage=critical,keyCertSign,cRLSign -out "$n-root.pem"
    openssl req -new -key "$n-int.key" -subj "/C=DK/O=Zephyr_A Test/CN=Test Int $n" -out "$n-int.csr"
    openssl x509 -req -in "$n-int.csr" -CA "$n-root.pem" -CAkey "$n-root.key" -CAcreateserial \
        -days 1000 -"$6" -extfile <(ext_int) -out "$n-int.pem" 2>/dev/null
    openssl req -new -key "$n-leaf.key" -subj "/CN=$5" -out "$n-leaf.csr"
    openssl x509 -req -in "$n-leaf.csr" -CA "$n-int.pem" -CAkey "$n-int.key" -CAcreateserial \
        -days 90 -"$6" -extfile <(ext_leaf "$5" "$7") -out "$n-leaf.pem" 2>/dev/null
    openssl x509 -in "$n-root.pem" -outform der -out "$n-root.der"
    cat "$n-leaf.pem" "$n-int.pem" > "$n-fullchain.pem"
}

chain ecdsa ecdsa384 ecdsa384 ecdsa256 "$HOST" sha384 ""
chain rsa rsa4096 rsa2048 rsa2048 "$HOST" sha256 ",keyEncipherment"
chain rogue ecdsa384 ecdsa384 ecdsa256 "$HOST" sha384 ""

# Leaf for another name, signed by the trusted ECDSA intermediate.
key wrongname-leaf.key ecdsa256
openssl req -new -key wrongname-leaf.key -subj "/CN=other.example.org" -out wrongname-leaf.csr
openssl x509 -req -in wrongname-leaf.csr -CA ecdsa-int.pem -CAkey ecdsa-int.key -CAcreateserial \
    -days 90 -sha384 -extfile <(ext_leaf other.example.org "") -out wrongname-leaf.pem 2>/dev/null
cp ecdsa-int.key wrongname-int.key
cat wrongname-leaf.pem ecdsa-int.pem > wrongname-fullchain.pem

# Leaves outside their validity period, signed by the trusted ECDSA
# intermediate (Mbed TLS date check, CONFIG_MBEDTLS_HAVE_TIME_DATE).
mkdir -p ca-db
: > ca-db/index.txt
echo 1000 > ca-db/serial
cat > ca.cnf <<EOF2
[ ca ]
default_ca = d
[ d ]
database = ca-db/index.txt
serial = ca-db/serial
new_certs_dir = ca-db
default_md = sha384
policy = p
unique_subject = no
[ p ]
commonName = supplied
[ ext ]
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature
extendedKeyUsage = serverAuth
subjectAltName = DNS:$HOST
EOF2
dated_leaf() { # <name> <start> <end>
    key "$1-leaf.key" ecdsa256
    openssl req -new -key "$1-leaf.key" -subj "/CN=$HOST" -out "$1-leaf.csr"
    openssl ca -batch -config ca.cnf -cert ecdsa-int.pem -keyfile ecdsa-int.key \
        -in "$1-leaf.csr" -out "$1-leaf.pem" -startdate "$2" -enddate "$3" \
        -extensions ext -notext 2>/dev/null
    cat "$1-leaf.pem" ecdsa-int.pem > "$1-fullchain.pem"
}
dated_leaf expired 20240101000000Z 20240401000000Z
dated_leaf notyet 20350101000000Z 20350401000000Z

# Pad the served chains like the real Let's Encrypt chains (cross-signed roots),
# so the record sizes the device receives are realistic.
if [ -n "$PAD" ]; then
    for f in "$PAD/gen-y/root-ye-by-x2.pem" "$PAD/gen-y/root-x2-by-x1.pem"; do
        cat "$f" >> ecdsa-fullchain.pem; cat "$f" >> wrongname-fullchain.pem; cat "$f" >> rogue-fullchain.pem
    done
    cat "$PAD/gen-y/root-yr-by-x1.pem" >> rsa-fullchain.pem
fi

der_size() { # total DER size of all certificates in a PEM file
    local total=0 tmp
    tmp=$(mktemp -d)
    csplit -s -z -f "$tmp/c" "$1" '/-----BEGIN CERTIFICATE-----/' '{*}'
    for c in "$tmp"/c*; do
        total=$((total + $(openssl x509 -in "$c" -outform der | wc -c)))
    done
    rm -rf "$tmp"
    echo "$total"
}

for n in ecdsa rsa rogue wrongname expired notyet; do
    printf '%-10s served chain: %s certs, %s B DER\n' "$n" "$(grep -c BEGIN "$n-fullchain.pem")" \
        "$(der_size "$n-fullchain.pem")"
done
