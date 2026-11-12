#!/usr/bin/env python3
"""Zephyr_A - small PKI for device client certificates (mTLS, part 10).

The device CA lives on the operator PC in certs/device-pki/ (ignored by git),
NOT on the server: a compromised server must not be able to issue device
identities. The server only gets the CA certificate and the CRL.

    python scripts/device_pki.py init                 create the device CA
    python scripts/device_pki.py issue <device-id>    key + certificate (CN = device id)
    python scripts/device_pki.py revoke <device-id>   revoke, write a new CRL
    python scripts/device_pki.py crl                  re-sign the CRL (new dates)
    python scripts/device_pki.py list                 issued / revoked certificates

Options: --dir <path> (default: <repo>/certs/device-pki), --days <n> for issue.
The CA key can be encrypted: set ZEPHYR_A_CA_PASSPHRASE before init and every
later use.

Files (PEM for people and the server, DER for the firmware build):
    ca.crt  ca.key  crl.pem  index.json
    devices/<id>.crt  <id>.key  <id>.crt.der  <id>.key.der

Algorithms: ECDSA P-256 / SHA-256 for CA and devices (fast on the
ESP32-C6, small handshake), same as the MCUboot signing key.
Only uses the "cryptography" package (installed with imgtool / MCUboot).
"""

import argparse
import datetime
import json
import os
import re
import sys
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

REPO = Path(__file__).resolve().parent.parent
ID_RE = re.compile(r"^[A-Za-z0-9._-]{1,64}$")  # same rule as cloud/scripts/60-add-device.sh
CA_DAYS = 3650
DEVICE_DAYS = 1825
# The CRL is published by us (copied to the server), not fetched from a URL.
# OpenSSL/Mosquitto refuse ALL clients once nextUpdate has passed, so it is
# valid for a long time; "crl" re-signs it (docs/10-nice-to-have.md).
CRL_DAYS = 3650


def now():
    return datetime.datetime.now(datetime.timezone.utc)


def passphrase():
    p = os.environ.get("ZEPHYR_A_CA_PASSPHRASE", "")
    return p.encode() if p else None


def write(path, data, private=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_bytes(data)
    if private:
        try:
            os.chmod(tmp, 0o600)
        except OSError:
            pass  # Windows: protected by the user profile ACL
    tmp.replace(path)


def load_ca(d):
    key_file, crt_file = d / "ca.key", d / "ca.crt"
    if not key_file.exists():
        sys.exit(f"No device CA in {d} - run: python scripts/device_pki.py init")
    key = serialization.load_pem_private_key(key_file.read_bytes(), passphrase())
    crt = x509.load_pem_x509_certificate(crt_file.read_bytes())
    return key, crt


def load_index(d):
    f = d / "index.json"
    return json.loads(f.read_text()) if f.exists() else {"issued": [], "revoked": []}


def save_index(d, idx):
    write(d / "index.json", (json.dumps(idx, indent=2) + "\n").encode())


def key_pem(key, encrypt=None):
    enc = (serialization.BestAvailableEncryption(encrypt) if encrypt
           else serialization.NoEncryption())
    return key.private_bytes(serialization.Encoding.PEM,
                             serialization.PrivateFormat.PKCS8, enc)


def write_crl(d, key, ca, idx):
    b = (x509.CertificateRevocationListBuilder()
         .issuer_name(ca.subject)
         .last_update(now() - datetime.timedelta(minutes=5))
         .next_update(now() + datetime.timedelta(days=CRL_DAYS)))
    for r in idx["revoked"]:
        b = b.add_revoked_certificate(
            x509.RevokedCertificateBuilder()
            .serial_number(int(r["serial"], 16))
            .revocation_date(datetime.datetime.fromisoformat(r["revoked_at"]))
            .build())
    crl = b.sign(key, hashes.SHA256())
    write(d / "crl.pem", crl.public_bytes(serialization.Encoding.PEM))
    return crl


def cmd_init(d, _args):
    if (d / "ca.key").exists():
        sys.exit(f"Device CA already exists in {d} (not overwritten)")
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Zephyr_A"),
                      x509.NameAttribute(NameOID.COMMON_NAME, "Zephyr_A Device CA")])
    ski = x509.SubjectKeyIdentifier.from_public_key(key.public_key())
    crt = (x509.CertificateBuilder()
           .subject_name(name).issuer_name(name)
           .public_key(key.public_key())
           .serial_number(x509.random_serial_number())
           .not_valid_before(now() - datetime.timedelta(minutes=5))
           .not_valid_after(now() + datetime.timedelta(days=CA_DAYS))
           .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
           .add_extension(x509.KeyUsage(digital_signature=False, content_commitment=False,
                                        key_encipherment=False, data_encipherment=False,
                                        key_agreement=False, key_cert_sign=True,
                                        crl_sign=True, encipher_only=False,
                                        decipher_only=False), critical=True)
           .add_extension(ski, critical=False)
           .sign(key, hashes.SHA256()))
    write(d / "ca.key", key_pem(key, passphrase()), private=True)
    write(d / "ca.crt", crt.public_bytes(serialization.Encoding.PEM))
    idx = {"issued": [], "revoked": []}
    save_index(d, idx)
    write_crl(d, key, crt, idx)
    print(f"Device CA created in {d}")
    print("  ca.key  - SECRET: keep a backup offline, never copy it to the server")
    print("  ca.crt, crl.pem - copy to the server (cloud/scripts/65-mtls.sh)")


def cmd_issue(d, args):
    dev = args.id
    if not ID_RE.match(dev):
        sys.exit(f"Invalid device id '{dev}' (allowed: A-Z a-z 0-9 . _ -, max 64)")
    ca_key, ca = load_ca(d)
    out = d / "devices"
    if (out / f"{dev}.key").exists() and not args.force:
        sys.exit(f"{out / (dev + '.key')} exists - use --force to issue a new one "
                 "(revoke the old certificate first)")
    key = ec.generate_private_key(ec.SECP256R1())
    serial = x509.random_serial_number()
    crt = (x509.CertificateBuilder()
           .subject_name(x509.Name([
               x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Zephyr_A"),
               x509.NameAttribute(NameOID.COMMON_NAME, dev)]))
           .issuer_name(ca.subject)
           .public_key(key.public_key())
           .serial_number(serial)
           .not_valid_before(now() - datetime.timedelta(minutes=5))
           .not_valid_after(now() + datetime.timedelta(days=args.days))
           .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
           .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
                                        key_encipherment=False, data_encipherment=False,
                                        key_agreement=False, key_cert_sign=False,
                                        crl_sign=False, encipher_only=False,
                                        decipher_only=False), critical=True)
           .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.CLIENT_AUTH]),
                          critical=False)
           .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ca.public_key()),
                          critical=False)
           .sign(ca_key, hashes.SHA256()))
    write(out / f"{dev}.key", key_pem(key), private=True)
    write(out / f"{dev}.key.der", key.private_bytes(serialization.Encoding.DER,
                                                    serialization.PrivateFormat.PKCS8,
                                                    serialization.NoEncryption()), private=True)
    write(out / f"{dev}.crt", crt.public_bytes(serialization.Encoding.PEM))
    write(out / f"{dev}.crt.der", crt.public_bytes(serialization.Encoding.DER))
    idx = load_index(d)
    idx["issued"].append({"id": dev, "serial": f"{serial:x}",
                          "not_after": crt.not_valid_after_utc.isoformat()})
    save_index(d, idx)
    print(f"Issued {dev}: serial {serial:x}, valid until {crt.not_valid_after_utc:%Y-%m-%d}")
    print(f"  {out / (dev + '.crt.der')}  {out / (dev + '.key.der')}  (used by build.ps1 -Mtls)")


def cmd_revoke(d, args):
    ca_key, ca = load_ca(d)
    idx = load_index(d)
    revoked = {r["serial"] for r in idx["revoked"]}
    hits = [i for i in idx["issued"] if i["id"] == args.id and i["serial"] not in revoked]
    if not hits:
        sys.exit(f"No valid certificate issued for {args.id}")
    for i in hits:
        idx["revoked"].append({"id": i["id"], "serial": i["serial"],
                               "revoked_at": now().replace(microsecond=0).isoformat()})
        print(f"Revoked {i['id']} serial {i['serial']}")
    save_index(d, idx)
    write_crl(d, ca_key, ca, idx)
    print(f"New CRL: {d / 'crl.pem'} - copy it to the server and run 65-mtls.sh again")


def cmd_crl(d, _args):
    ca_key, ca = load_ca(d)
    crl = write_crl(d, ca_key, ca, load_index(d))
    print(f"CRL re-signed, valid until {crl.next_update_utc:%Y-%m-%d}: {d / 'crl.pem'}")


def cmd_list(d, _args):
    idx = load_index(d)
    revoked = {r["serial"] for r in idx["revoked"]}
    for i in idx["issued"]:
        state = "REVOKED" if i["serial"] in revoked else "valid"
        print(f"{i['id']:<32} {i['serial']:<40} until {i['not_after'][:10]}  {state}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", type=Path, default=REPO / "certs" / "device-pki")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("init")
    p = sub.add_parser("issue")
    p.add_argument("id")
    p.add_argument("--days", type=int, default=DEVICE_DAYS)
    p.add_argument("--force", action="store_true")
    p = sub.add_parser("revoke")
    p.add_argument("id")
    sub.add_parser("crl")
    sub.add_parser("list")
    args = ap.parse_args()
    {"init": cmd_init, "issue": cmd_issue, "revoke": cmd_revoke,
     "crl": cmd_crl, "list": cmd_list}[args.cmd](args.dir, args)


if __name__ == "__main__":
    main()
