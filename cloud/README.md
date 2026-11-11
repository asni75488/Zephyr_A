# cloud/

Server side of Project A on the Webdock VPS (`zephyra.vps.webdock.cloud`,
Ubuntu 24.04 "Noble", LEMP image). Step-by-step guide: [docs/05-cloud.md](../docs/05-cloud.md).

| Path | Purpose |
|---|---|
| `scripts/10-base.sh` | Packages (mosquitto, clients), automatic security updates, NTP check |
| `scripts/20-firewall.sh` | UFW: 22, 80, 443, 1880, 8883 open; FTP closed |
| `scripts/30-certs.sh` | Find/request the Let's Encrypt certificate, install the deploy hook |
| `scripts/40-mosquitto.sh` | Mosquitto config, ACL, password file, `nodered` user |
| `scripts/50-nodered.sh` | Node-RED + Dashboard 2.0, logins, HTTPS |
| `scripts/60-add-device.sh` | Add an edge node (MQTT user = Sparkplug edge node id) |
| `scripts/90-verify.sh` | Read-only check of the whole setup |
| `mosquitto/zephyr_a.conf` | Broker config: 8883 TLS, 1883 localhost only, no anonymous |
| `mosquitto/acl` | Least-privilege ACL (`pattern ... %u`); contains no secrets |
| `letsencrypt/zephyr-a-deploy-hook.sh` | Copies renewed certificates to Mosquitto/Node-RED and restarts them |
| `node-red/settings-zephyr_a.js` | Node-RED settings overlay (HTTPS, logins, credentialSecret) |
| `node-red/flows.json` | Flow: MQTT → Sparkplug decode → data log + dashboard |
| `node-red/decode.js` | Source of the decode function (unit test in `node-red/test/`) |
| `tools/spb-test-publisher.js` | Simulated edge node (TLS, Sparkplug B, batching) |

Run on the server, in this order:

```bash
git clone -b del-05-cloud https://github.com/asni75488/Zephyr_A.git ~/Zephyr_A
cd ~/Zephyr_A/cloud/scripts
sudo bash 10-base.sh && sudo bash 20-firewall.sh && sudo bash 30-certs.sh && sudo bash 40-mosquitto.sh
sudo bash 60-add-device.sh esp32c6-test0001
sudo bash 50-nodered.sh
sudo bash 90-verify.sh
```

No secrets are stored in this folder: MQTT passwords live (hashed) in
`/etc/mosquitto/passwd`, Node-RED logins (bcrypt) and `credentialSecret` in
`~/.node-red/zephyr_a-secrets.json` on the server only.
