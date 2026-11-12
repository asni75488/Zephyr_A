# Changelog

One entry per part (branch `del-NN-...`, pull request against `main`). The
version is in `app/VERSION` and is reported by the device as
`Properties/FW Version`. Details: `docs/NN-*.md`, status: `docs/STATUS.md`.

## 0.11.0-del11 – Wrap-up: test, memory layout, report material
- `scripts/memory_layout.py`: flash/HP-SRAM/LP-SRAM report from the build, `--check` limits
- `scripts/run-host-tests.sh` and GitHub Actions workflow `host-tests` (tools + firmware jobs)
- `app/overlay-stack-usage.conf` for stack measurement on hardware
- Hardware acceptance protocol and report material (`docs/11-afslutning.md`)

## 0.10.0-del10 – Nice to have
- Task watchdog for all threads with hardware fallback (`health.c`), stuck thread in NBIRTH
- fail2ban for MQTT and SSH (`cloud/fail2ban`, `70-fail2ban.sh`)
- mTLS: device CA on the PC (`device_pki.py`), `build.ps1 -Mtls`, listener 8884 with CRL
- Dashboard: edge node table and command controls
- OTA postponed

## 0.9.0-del09 – Low power and data, LP core
- LP-core image (`app/remote`): deep-sleep wake-up and supervisor, image hash check
- Light sleep, interval uplink (`-LowPower`), modem radio off, NCMD Sleep
- Clean session end (NDEATH + DISCONNECT), `is_historical`, NCMD queue in Node-RED

## 0.8.0-del08 – Security
- MCUboot ECDSA P-256 signing, slot0 validation, downgrade prevention
- SAR entropy, eFuse posture, `SECURITY:` events, reboot guard, stack canaries
- Release variant, SBOM, IEC 62443-4-2 / CRA mapping, `SECURITY.md`

## 0.7.0-del07 – Sparkplug B and time
- Sparkplug B 3.0 with nanopb (BIRTH/DATA/DEATH, aliases, bdSeq, NCMD)
- SNTP time synchronisation, Node-RED decode and NCMD encoder

## 0.6.0-del06 – MQTT over TLS
- MQTT client over TLS 1.3/1.2 (Mbed TLS 4.1, PSA), four embedded root CAs
- native_sim test suite against a real Mosquitto broker

## Parts 1–5
- 1: Windows 11 setup, west T2 manifest, build/flash scripts, devcontainer
- 2: Architecture, requirement matrix, hardware analysis
- 3: Data acquisition (k_timer, coretemp, Modbus RTU, simulation)
- 4: Cellular network (SIM7080G, CMUX, PPP, Onomondo)
- 5: Cloud server (Mosquitto, Node-RED, certbot, UFW)
