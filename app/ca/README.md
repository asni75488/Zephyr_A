# app/ca – trusted root CA certificates

The firmware trusts **only** these four public root certificates for the TLS
connection to the MQTT broker (`zephyra.vps.webdock.cloud`, Let's Encrypt
certificate). They are embedded at build time (`app/cmake/ca_certs.cmake`,
DER, kept in flash) and registered as TLS credentials by `src/tls_ca.c`.

Root certificates are public. They contain no private key and are not
secrets, so they are committed (exception in `.gitignore`).

| File | Subject | Key | Valid until | SHA-256 fingerprint |
|---|---|---|---|---|
| `isrg-root-ye.der` | ISRG Root YE | ECDSA P-384 | 2045-09-02 | `E1:4F:FC:AD:5B:00:25:73:10:06:CA:A4:3A:12:1A:22:D8:E9:70:0F:4F:B9:CF:85:2F:02:A7:08:AA:5D:56:66` |
| `isrg-root-yr.der` | ISRG Root YR | RSA 4096 | 2045-09-02 | `E5:7B:7E:6F:15:0C:41:91:02:E8:D5:C0:55:72:9F:F9:67:B9:D1:A8:29:BF:00:CE:C8:9C:A6:04:EB:F4:A8:6F` |
| `isrg-root-x2.der` | ISRG Root X2 | ECDSA P-384 | 2040-09-17 | `69:72:9B:8E:15:A8:6E:FC:17:7A:57:AF:B7:17:1D:FC:64:AD:D2:8C:2F:CA:8C:F1:50:7E:34:45:3C:CB:14:70` |
| `isrg-root-x1.der` | ISRG Root X1 | RSA 4096 | 2035-06-04 | `96:BC:EC:06:26:49:76:F3:74:60:77:9A:CF:28:C5:A7:CF:E8:A3:C0:AA:E1:1A:8F:FC:EE:05:C0:BD:DF:08:C6` |

**Why these four:** since May/June 2026 Let's Encrypt issues from the
"Generation Y" hierarchy. The server sends
`leaf <- YE1/YE2 <- Root YE <- ISRG Root X2 (<- X1)` for ECDSA certificates and
`leaf <- YR1/YR2 <- Root YR <- ISRG Root X1` for RSA certificates. Trusting
Root YE/YR directly means the chain is verified with one signature check
above the intermediate, and works even if the server one day sends a short
chain without the cross-signed certificates. X1/X2 cover the older chains.

**Source:** Let's Encrypt's own repository
(`github.com/letsencrypt/website`, `static/certs/gen-y/root-ye.der`,
`root-yr.der`, `isrg-root-x2.der`, `isrgrootx1.der`). X1 and X2 are
byte-identical with the copies in Ubuntu's `ca-certificates`. YE/YR were
checked with `openssl verify -partial_chain` against the YE2/YR2
intermediates.

Check a file on Windows:

```powershell
certutil -hashfile app\ca\isrg-root-ye.der SHA256
```

**Renewal:** the roots are valid until 2035-2045. When Let's Encrypt
introduces a new root, add the DER file here and to the list in
`app/CMakeLists.txt` (and raise `CONFIG_TLS_MAX_CREDENTIALS_NUMBER` /
`CONFIG_NET_SOCKETS_TLS_MAX_CREDENTIALS` above 4 if needed). Without
over-the-air updates (part 10) this requires a new firmware.
