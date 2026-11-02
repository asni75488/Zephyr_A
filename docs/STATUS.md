# STATUS – Zephyr_A (Projekt A)

Opdateres i slutningen af hver del.

| Del | Emne | Branch | Status |
|---|---|---|---|
| 1 | Udviklingsmiljø Windows 11 + repo-skelet | `del-01-setup` | Færdig – afventer test på hardware |
| 2 | Design: arkitektur, tråde, pin-tabel | `del-02-design` | Ikke startet |
| 3 | Cellulært netværk (SIM7080G, PPP) | – | Ikke startet |
| 4 | Cloud-server (Webdock: SSH, Mosquitto, Let's Encrypt, Node-RED) | – | Ikke startet |
| 5 | MQTT + TLS | – | Ikke startet |
| 6 | Dataopsamling (intern temperatursensor, Modbus senere) | – | Ikke startet |
| 7 | Sparkplug B (nanopb) | – | Ikke startet |
| 8 | Lavt strøm- og dataforbrug, LP-core | – | Ikke startet |
| 9 | Sikkerhed: IEC 62443-4-2 / CRA | – | Ikke startet |
| 10 | Nice to have (NCMD, dashboard, mTLS, MCUboot-signering/OTA) | – | Ikke startet |
| 11 | Afslutning: test, memory layout, rapportmateriale | – | Ikke startet |

## Del 1 – lavet

- `west.yml` (T2): Zephyr pinnet til **v4.4.0**, moduler: hal_espressif, mbedtls, mcuboot, nanopb
- Zephyr SDK-version fastlagt fra `zephyr/SDK_VERSION` = **1.0.1** (kun `riscv64-zephyr-elf`)
- Board target verificeret: `esp32c6_devkitc/esp32c6/hpcore` (LP: `esp32c6_devkitc/esp32c6/lpcore`)
- `scripts/setup.ps1` (idempotent), `build.ps1`, `flash.ps1 -Port COMx`, `monitor.ps1 -Port COMx`
- App-skelet efter Micro Technics struktur (`app/`, `app/remote/` tom med README)
- Firmware: logger `boot`, device-id (MAC via hwinfo), Zephyr-version, app-version, reset-årsag, heartbeat hvert 5. s
- `.gitignore` udelukker build, venv, `secrets.conf`, certifikater og nøgler. `app/secrets.conf.example` er committet
- Dokumentation: `docs/01-opsaetning-windows.md`, `docs/hardware.md` (pin-tabel)
- Valgfri `.devcontainer/` (zephyr-build:v0.29.1)

### Verificeret

- Firmwaren er testbygget (sysbuild: MCUboot + app) med netop denne `west.yml` og SDK 1.0.1 i et
  Linux-miljø: 0 compiler-warnings, FLASH 135 kB (1,6 %), RAM 50 kB (10 %).
- `west flash`-kommandoen er kontrolleret: MCUboot flashes til 0x0 før app'en.
- Alle PowerShell-scripts er syntakstjekket med PowerShell 7.4.

### Mangler (test på Windows/hardware)

- [ ] `setup.ps1` kørt på Windows 11
- [ ] `build.ps1`, `flash.ps1`, `monitor.ps1` testet på boardet

## Kendte problemer / noter

- Sysbuild giver en harmløs Kconfig-warning: `MCUBOOT_UPDATE_FOOTER_SIZE ... unsatisfied
  dependencies: MCUBOOT_IMG_MANAGER`. Den kommer fra sysbuilds MCUboot-integration og er kun
  relevant for OTA (nice-to-have). Den påvirker ikke buildet.
- Espressif-blobs kunne ikke hentes i testmiljøet (netværksbegrænsning). Det har ingen betydning
  for del 1, fordi Wi-Fi/BLE ikke bruges, men `setup.ps1` henter dem på Windows.
- Devcontaineren er ikke testet (valgfri).
- Pc'en har ældre installationer (`C:\zephyrproject` = 4.4.0, `C:\Users\paulm\zephyrproject` = 4.5.99).
  De bruges ikke. `zephyr.base-prefer=configfile` sikrer, at west bruger `C:\zp`.
- Modulet (SIM7080G) har ingen PWRKEY/RESET-stift, se `docs/hardware.md`.
