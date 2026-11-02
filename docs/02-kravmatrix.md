# Kravmatrix – Projekt A

Alle krav fra kravspecifikationen (generelle krav, Projekt A-specifikke krav, nice to have og
out of scope) samt arbejdsreglerne for projektet. Matrixen opdateres i slutningen af hver del.

**Status:** ✅ opfyldt og testet · 🟡 delvist / design klar · ⬜ ikke startet · ➖ out of scope

**Plan for delene:**
1 Setup · 2 Design · 3 Cellulært netværk · 4 Cloud-server · 5 MQTT + TLS · 6 Dataopsamling ·
7 Sparkplug B · 8 Sikkerhed (IEC 62443-4-2 / CRA) · 9 Lavt strøm/data + LP-kerne ·
10 Nice to have · 11 Afslutning

## Generelle krav

| ID | Krav | Hvordan det løses | Modul / fil | Del | Status | Test |
|---|---|---|---|---|---|---|
| G-01 | Zephyr 4.4.0 | `west.yml` pinner `zephyr` til `v4.4.0` (T2-manifest) | `west.yml` | 1 | ✅ | `west list` viser v4.4.0. Boot-log: `Zephyr version: 4.4.0` |
| G-02 | Zephyr SDK der matcher 4.4.0 | SDK-version læst fra `zephyr/SDK_VERSION` = 1.0.1. Kun `riscv64-zephyr-elf` | `scripts/setup.ps1` | 1 | ✅ | `setup.ps1` finder SDK 1.0.1. Build OK |
| G-03 | Kompatibel med min. C17 | `CONFIG_STD_C17=y` (compiler kører `-std=c17`) | `app/prj.conf` | 1 | ✅ | Build uden warnings med `-std=c17` |
| G-04 | Udviklingsmiljø: Windows 11 host-OS (devcontainer valgfri) | PowerShell-scripts. Valgfri `.devcontainer/` | `scripts/`, `.devcontainer/` | 1 | ✅ | DoD Del 1 testet på Windows 11 |
| G-05 | `prj.conf` styrer Kconfig | Al konfiguration i `prj.conf`, projektindstillinger i `app/Kconfig`, hemmeligheder i `secrets.conf` | `app/prj.conf`, `app/Kconfig` | 1–2 | ✅ | Review |
| G-06 | Devicetree til hardware. Afvigelser i `.overlay` | `app/app.overlay` (UART1, modem, coretemp). Flash-variant via officiel snippet | `app/app.overlay`, `scripts/build.ps1` | 2–6 | 🟡 | Overlay-plan prototype-bygget (Del 2). Hardware-test i Del 3/6 |
| G-07 | ALLE afvigelser (fx GPIO/UART-pins) dokumenteres | Pin-tabel og afvigelser i `docs/hardware.md`, plan i `02-arkitektur.md` §6 | `docs/hardware.md` | 1–6 | 🟡 | Review: hver overlay-ændring har en række i `hardware.md` |
| G-08 | Zephyrs egne API'er (netværk, modem, MQTT, Modbus, logging, PM) | `modem_cellular`, net stack/PPP, `mqtt`, `modbus`, `log`, `pm`, `sensor`, `hwinfo`, nanopb-modul | alle moduler | 2–9 | 🟡 | Review: ingen egne AT-/socket-drivere |
| G-09 | Multithreaded, ca. 1 thread pr. modul (netværk, app-logik, Sparkplug, MQTT) | Tråde: `sensor`, `network`, `mqtt`, `app_logic`, `sparkplug` | `02-arkitektur.md` §3 | 2–7 | 🟡 | Thread analyzer / `kernel threads` viser trådene |
| G-10 | Delte data beskyttes med mutex/semaphore/message queue | `k_msgq` (sample/batch/cmd/tx/rx), `k_mutex` (config), `k_event` (netværksstatus), `k_sem` (trigger), `k_mem_slab` (payloads) | `02-arkitektur.md` §4 | 2–7 | 🟡 | Review + test under belastning |
| G-11 | Modulopdelt, hvert modul egen `.c` og `.h` | 9 moduler + `app_types.h` | `app/src/*` | 2 | ✅ | Skelet bygger. Én `.c/.h` pr. modul |
| G-12 | Mappestruktur efter Micro Technics eksempel | `app.overlay`, `CMakeLists.txt`, `Kconfig.sysbuild`, `prj.conf`, `sample.yaml`, `sysbuild.cmake`, `src/`, `remote/` | `app/` | 1 | ✅ | Review |
| G-13 | LP-core-kode i `remote/` (`boards/esp32c6_devkitc_lpcore.overlay`, `CMakeLists.txt`, `prj.conf`, `src/main_ULP.c`) | Sysbuild `ExternalZephyrProject_Add`, board `esp32c6_devkitc/esp32c6/lpcore` | `app/remote/`, `app/sysbuild.cmake` | 9 | ⬜ | Build af begge images. LP-kerne vækker HP-kerne |
| G-14 | TLS (self-signed CA eller Let's Encrypt/certbot) | Let's Encrypt via certbot (serveren har domænet `zephyra.vps.webdock.cloud`). Mbed TLS 4.1 + PSA | `mqtt_client.c`, `cloud/` | 4–5 | ⬜ | Broker afviser plain MQTT. Enhed forbinder kun med gyldigt cert |
| G-15 | MQTT | Zephyr MQTT-bibliotek (3.1.1) over TLS-socket | `mqtt_client.c` | 5 | ⬜ | Beskeder ses på brokeren (`mosquitto_sub`) |
| G-16 | Modbus RTU/TCP til dataopsamling | `modbus.c` med Zephyr Modbus client. Interface (RTU/TCP) afklares | `modbus.c/.h` | 6+ | ⬜ | Afventer Modbus-kilde |
| G-17 | Sparkplug B (`spBv1.0`) som beskedstandard | nanopb + Tahu `sparkplug_b.proto`. NBIRTH/DBIRTH/DDATA/NDEATH, bdSeq/seq | `sparkplug.c/.h` | 7 | 🟡 | Topic-builder i skelet. Decode i Node-RED/Sparkplug-værktøj |
| G-18 | Logging via logging-subsystemet | `CONFIG_LOG`, deferred mode | alle `.c` | 1 | ✅ | Boot-log |
| G-19 | SKAL logge: netværksstatus | `network` logger hver tilstandsovergang | `network.c` | 3 | ⬜ | Log viser `starting → registering → ppp-up → connected` |
| G-20 | SKAL logge: cloud-forbindelse | `mqtt` logger connect/disconnect/fejl | `mqtt_client.c` | 5 | ⬜ | Log ved CONNACK og afbrydelse |
| G-21 | SKAL logge: errors | `LOG_ERR` ved alle fejlstier (returkoder tjekkes) | alle | 1– | 🟡 | Fremprovokerede fejl (fx antenne af) logges |
| G-22 | SKAL logge: custom logs | Device-id, reset-årsag, konfiguration, heartbeat, batch-statistik | `device_info.c`, `app_logic.c` | 1– | ✅ | Boot-log |
| G-23 | Log levels korrekt | ERR = fejl, der kræver handling. WRN = afvigelse (fx brownout). INF = tilstandsskift. DBG = detaljer. `CONFIG_APP_LOG_LEVEL` | alle | 1– | 🟡 | Review |
| G-24 | `LOG_MODULE_REGISTER` i hver `.c`-fil | Én pr. fil, unikt modulnavn | alle `.c` | 2 | ✅ | `grep LOG_MODULE_REGISTER app/src/*.c` |
| G-25 | Hver MQTT-besked: device-id, timestamp og aflæste værdier | device-id = edge node id i topic. Sparkplug `timestamp` + pr. metric. Værdier = metrics | `sparkplug.c` | 7 | 🟡 | Decode af DDATA viser alle tre |

## Projekt A-specifikke krav

| ID | Krav | Hvordan det løses | Modul / fil | Del | Status | Test |
|---|---|---|---|---|---|---|
| A-01 | Cellulær forbindelse til cloud via udleveret SIM-kort | M5Stamp CAT-M (SIM7080G), `modem_cellular` (`simcom,sim7080`), PPP, APN `onomondo` | `network.c`, `app.overlay` | 3 | ⬜ | PPP up + IP-adresse i log. Ping/DNS |
| A-02 | Sikker kommunikation (TLS) | Se G-14. Serverens certifikat verificeres mod CA. Hostname-check (SNI) | `mqtt_client.c` | 5 | ⬜ | Forkert CA → forbindelse afvises (negativ test) |
| A-03 | Lavt strømforbrug via sleep modes eller interrupts | PM light sleep i idle, event-drevne tråde, modem PSM/eDRX, deep sleep + LP-kerne | `power.c`, `network.c`, `remote/` | 9 | 🟡 | Multimetermåling før/efter (mA) |
| A-04 | Lavt dataforbrug via batching eller små payloads | Batch 6 målinger/DDATA, protobuf, metric aliases, QoS 0, lang keep-alive | `app_logic.c`, `sparkplug.c` | 7, 9 | 🟡 | Bytes/døgn (Onomondo) med og uden batching |
| A-05 | SKAL bruge egen cloud-server til dataopsamling | Webdock VPS `zephyra.vps.webdock.cloud`: Mosquitto + Node-RED | `cloud/` | 4 | ⬜ | Data modtages og vises på serveren |
| A-06 | Dataopsamling med fast interval (5–15 s) | `APP_SAMPLE_INTERVAL_MS` (range 5000–15000, standard 10000). Absolutte deadlines → ingen drift | `sensor.c`, `app/Kconfig` | 2, 6 | 🟡 | Kconfig afviser 20000 (testet Del 2). Tidsstempler i DDATA med 10 s ± få ms |
| A-07 | Data sendes i MQTT som Sparkplug B | Se G-17 | `sparkplug.c` | 7 | ⬜ | |
| A-08 | IEC 62443-4-2 og/eller CRA Annex I (1.c fortrolighed, 1.d integritet) kan redegøres for | Mapping-dokument. Fx CR 4.1 / CRA 1.c (TLS-kryptering), CR 3.1 / CRA 1.d (TLS-integritet, MCUboot), CR 1.x (MQTT-autentifikation) | `docs/08-sikkerhed.md` | 8 | ⬜ | Review |
| A-09 | Mindst 2 krav implementeret/identificeret | Kandidater: kommunikationsfortrolighed (TLS), integritet (TLS + signeret firmware), autentifikation (brugernavn/password → mTLS), ingen hemmeligheder i git | se A-08 | 8 | ⬜ | Test pr. krav |
| A-10 | Relevant security level vurderes | SL-vurdering (SL 1/2) med begrundelse | `docs/08-sikkerhed.md` | 8 | ⬜ | Review |

## Nice to have

| ID | Krav | Hvordan det løses | Modul / fil | Del | Status | Test |
|---|---|---|---|---|---|---|
| N-01 | Kommandoer fra cloud via MQTT/Sparkplug (NCMD) | Rebirth, Reboot, Sample Interval, Batch Size → `cmd_q` → `app_logic` | `sparkplug.c`, `app_logic.c` | 7/10 | 🟡 | Send NCMD fra Node-RED → ændring i log |
| N-02 | OTA | MCUboot (allerede bootloader) + download over MQTT/HTTP | – | 10 | ⬜ | |
| N-03 | Dashboard | Node-RED dashboard på VPS'en | `cloud/node-red/` | 4/10 | ⬜ | |
| N-04 | mTLS | Klientcertifikat på enheden, `require_certificate` i Mosquitto | `mqtt_client.c`, `certs/` | 10 | ⬜ | |
| N-05 | Bootloader-verifikation (MCUboot) | Signering slået til (`SB_CONFIG_BOOT_SIGNATURE_TYPE_*`), nøgle uden for git | `sysbuild` | 10 | ⬜ | Usigneret image afvises |
| N-06 | Memory layout-diagram | Fra `build/*/zephyr.map` + partitionstabel (4 MB) | `docs/` | 11 | ⬜ | |

## Out of scope

| ID | Emne | Status |
|---|---|---|
| O-01 | Avanceret PKI | ➖ |
| O-02 | Avanceret cloud-drift/skalering | ➖ |

## Arbejdsregler (proces)

| ID | Regel | Hvordan | Status |
|---|---|---|---|
| P-01 | Hver del på egen branch, små Conventional Commits, PR mod `main` | `del-01-setup`, `del-02-design`, … | ✅ |
| P-02 | Hemmeligheder committes aldrig | `.gitignore` (secrets.conf, certs, nøgler). `secrets.conf.example`. MQTT-login kun via `secrets.conf` | ✅ |
| P-03 | Docs-afsnit og `STATUS.md` pr. del | `docs/0X-*.md`, `docs/STATUS.md` | ✅ |
| P-04 | Byg/flash/test-kommandoer til PowerShell på Windows 11 | `scripts/*.ps1` | ✅ |
