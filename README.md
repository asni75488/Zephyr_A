# Zephyr_A – Projekt A (Industriel IoT, Micro Technic)

ESP32-C6-DevKitC med Zephyr RTOS 4.4.0, der opsamler data med fast interval og
sender dem som **Sparkplug B** over **MQTT/TLS** via et **cellulært modem**
(M5Stamp CAT-M, SIM7080G) til en egen server (Webdock VPS). Fokus på lavt strøm-
og dataforbrug.

> Studieprojekt, IT-teknolog, UCL, 3. semester – faget Industriel IoT.

## Hurtig start (Windows 11, PowerShell)

```powershell
# Engang: tillad lokale scripts og venv-aktivering
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser

# Klon repoet ind i en kort workspace-sti
New-Item -ItemType Directory -Force C:\zp | Out-Null
git -c core.longpaths=true clone https://github.com/asni75488/Zephyr_A.git C:\zp\zephyr_a
cd C:\zp\zephyr_a

.\scripts\setup.ps1                  # værktøjer, venv, west, Zephyr 4.4.0, SDK, blobs
.\scripts\new-signing-key.ps1        # én gang: egen MCUboot-signeringsnøgle (certs\, ikke i git)
.\scripts\build.ps1                  # sysbuild: MCUboot + signeret app + LP-kerne (-LowPower: docs/09, -Release/-Sbom: docs/08)
.\scripts\new-device-cert.ps1 -DeviceId <id>   # valgfrit: klientcertifikat (mTLS), derefter build.ps1 -Mtls (docs/10)
.\scripts\flash.ps1   -Port COM5     # "UART"-USB-porten på boardet
.\scripts\monitor.ps1 -Port COM5     # log (afslut: Ctrl+T og derefter X)
```

Detaljeret vejledning: [docs/01-opsaetning-windows.md](docs/01-opsaetning-windows.md).
Sikkerhed: [docs/08-sikkerhed.md](docs/08-sikkerhed.md) og [SECURITY.md](SECURITY.md).

## Struktur

```
zephyr_a/                 <- west manifest-repo (T2) – ligger i C:\zp\zephyr_a
├── west.yml              Pinner Zephyr v4.4.0 + nødvendige moduler
├── app/                  Firmware (Micro Technics mappestruktur)
│   ├── app.overlay       Devicetree-afvigelser (dokumenteret i docs/hardware.md)
│   ├── CMakeLists.txt
│   ├── Kconfig.sysbuild  Sysbuild-konfiguration (board for LP-kernen)
│   ├── prj.conf          Kconfig for HP-kernen
│   ├── sample.yaml       Twister-metadata
│   ├── sysbuild.cmake    Tilføjer LP-core-image (remote/) til buildet
│   ├── VERSION           Firmware-version
│   ├── secrets.conf.example  Skabelon til app/secrets.conf (MQTT-password, ignoreres af git)
│   ├── ca/               Rodcertifikater (ISRG) som enheden stoler på - offentlige
│   ├── cmake/, config/   CA-indlejring, LP-billedets hash, Mbed TLS-tilpasning
│   ├── common/           lp_shared.h: hukommelse og logik delt mellem HP- og LP-kernen
│   ├── proto/            Sparkplug B .proto (Eclipse Tahu) + nanopb-options
│   ├── src/              main.c + moduler (.c/.h pr. modul)
│   ├── tests/            Test på native_sim (mqtt_tls: TLS + Sparkplug B + uplink mod rigtig Mosquitto, lp_supervisor)
│   └── remote/           LP-core (ULP) image: deep-sleep-vækning og supervisor
├── certs/                Lokale certifikater/nøgler (ignoreres af git)
├── cloud/                Server: Mosquitto, Node-RED
├── docs/                 Dokumentation pr. del + STATUS.md
├── scripts/              setup/build/flash/monitor (PowerShell)
└── .devcontainer/        Valgfrit Docker-miljø
```

## Dokumentation

| Del | Dokument | Indhold |
|---|---|---|
| 1 | [01-opsaetning-windows.md](docs/01-opsaetning-windows.md) | Værktøjer, west-workspace, byg/flash på Windows 11 |
| 2 | [02-arkitektur.md](docs/02-arkitektur.md), [02-kravmatrix.md](docs/02-kravmatrix.md), [hardware.md](docs/hardware.md) | Moduler, tråde, pins, fund F1–F8, krav og status |
| 3 | [03-dataopsamling.md](docs/03-dataopsamling.md) | Måling, Modbus RTU, simulering |
| 4 | [04-netvaerk.md](docs/04-netvaerk.md) | SIM7080G, PPP, Onomondo |
| 5 | [05-cloud.md](docs/05-cloud.md) | VPS: Mosquitto, Node-RED, certbot, firewall |
| 6 | [06-mqtt-tls.md](docs/06-mqtt-tls.md) | MQTT over TLS 1.3, rod-CA'er |
| 7 | [07-sparkplug-tid.md](docs/07-sparkplug-tid.md) | Sparkplug B, metrics, SNTP |
| 8 | [08-sikkerhed.md](docs/08-sikkerhed.md), [SECURITY.md](SECURITY.md) | Trusselsmodel, SL, IEC 62443-4-2/CRA, signeret firmware |
| 9 | [09-strom-data.md](docs/09-strom-data.md) | Light/deep sleep, interval-uplink, LP-kernen, databudget |
| 10 | [10-nice-to-have.md](docs/10-nice-to-have.md) | Task watchdog, fail2ban, mTLS, dashboard |
| 11 | [11-afslutning.md](docs/11-afslutning.md) | Memory layout, test, hardware-acceptprotokol, rapportmateriale |

Ændringer pr. del: [CHANGELOG.md](CHANGELOG.md). Status: [docs/STATUS.md](docs/STATUS.md).

## Test

```bash
# Linux, WSL eller devcontainer: alle host-tests (ztest, native_sim mod Mosquitto, Node-RED, PKI, fail2ban, ShellCheck)
scripts/run-host-tests.sh <west-workspace> [<firmware build dir>]
```

Kører også automatisk på GitHub ved hvert push (`.github/workflows/host-tests.yml`).
Hardwaretest: [docs/11-afslutning.md](docs/11-afslutning.md) §4.
