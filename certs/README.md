# certs/

Local folder for keys and certificates. **Everything in this folder except
this README and `*.example` files is ignored by git** (see `.gitignore`).

| File | Secret? | Purpose | Since |
|---|---|---|---|
| `mcuboot-signing-key.pem` | **yes** | ECDSA P-256 private key that signs the firmware. MCUboot only boots images signed with it (`docs/08-sikkerhed.md`). Created with `scripts\new-signing-key.ps1` | part 8 |
| `mcuboot-signing-key.pub.pem` | no | Public part of the key (for review / documentation) | part 8 |
| `device.crt` / `device.key` | key is secret | Client certificate for mTLS (nice to have) | part 10 |

The root CAs the device trusts for the MQTT broker are public and live in
`app/ca/` (committed).

**Back up `mcuboot-signing-key.pem`** (password manager / encrypted USB stick).
Its public key is compiled into MCUboot: without the private key no new
firmware can be installed on a device without also reflashing the bootloader
over USB (and with hardware secure boot enabled not at all).

Private keys must never be committed. If one is committed by mistake, it must
be considered compromised and replaced — removing it from git history is not
enough.
