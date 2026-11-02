# certs/

Local folder for TLS certificates and keys. **Everything in this folder except
this README and `*.example` files is ignored by git** (see `.gitignore`).

Planned content (part 5/6):

| File | Secret? | Purpose |
|---|---|---|
| `ca.crt` | no (public) | Root CA the device uses to verify the broker (Let's Encrypt chain) |
| `device.crt` / `device.key` | key is secret | Client certificate for mTLS (nice to have) |

Private keys must never be committed. If one is committed by mistake, it must
be considered compromised and replaced — removing it from git history is not
enough.
