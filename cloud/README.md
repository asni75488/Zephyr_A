# cloud/

Server side of Project A, running on the Webdock VPS
(`zephyra.vps.webdock.cloud`, Ubuntu 24.04 "Noble", LEMP image).

Filled in part 4 (cloud server). Planned content:

| Path | Purpose |
|---|---|
| `mosquitto/` | Mosquitto MQTT broker config (TLS on port 8883, users, ACL) |
| `node-red/` | Node-RED flows: Sparkplug B decoding + dashboard |
| `scripts/` | Setup scripts for the server (firewall, certbot, services) |

Secrets (MQTT passwords, ACL files with real users) are kept out of git —
only `*.example` files are committed.
