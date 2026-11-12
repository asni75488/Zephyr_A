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

## Status

Se [docs/STATUS.md](docs/STATUS.md).
