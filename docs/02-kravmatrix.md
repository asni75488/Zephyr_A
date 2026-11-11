# Kravmatrix – Projekt A

Alle krav fra kravspecifikationen (generelle krav, Projekt A-specifikke krav, nice to have og
out of scope) samt arbejdsreglerne for projektet. Matrixen opdateres i slutningen af hver del.

**Status:** ✅ opfyldt og testet · 🟡 delvist / design klar · ⬜ ikke startet · ➖ out of scope

**Plan for delene** (omnummereret i Del 3):
1 Setup · 2 Design · 3 Dataopsamling · 4 Cellulært netværk · 5 Cloud-server · 6 MQTT + TLS ·
7 Sparkplug B + tid · 8 Sikkerhed (IEC 62443-4-2 / CRA) · 9 Lavt strøm/data + LP-kerne ·
10 Nice to have · 11 Afslutning

*Sidst opdateret: Del 7.*

## Generelle krav

| ID | Krav | Hvordan det løses | Modul / fil | Del | Status | Test |
|---|---|---|---|---|---|---|
| G-01 | Zephyr 4.4.0 | `west.yml` pinner `zephyr` til `v4.4.0` (T2-manifest) | `west.yml` | 1 | ✅ | `west list` viser v4.4.0. Boot-log: `Zephyr version: 4.4.0` |
| G-02 | Zephyr SDK der matcher 4.4.0 | SDK-version læst fra `zephyr/SDK_VERSION` = 1.0.1. Kun `riscv64-zephyr-elf` | `scripts/setup.ps1` | 1 | ✅ | `setup.ps1` finder SDK 1.0.1. Build OK |
| G-03 | Kompatibel med min. C17 | `CONFIG_STD_C17=y` (compiler kører `-std=c17`) | `app/prj.conf` | 1 | ✅ | Build uden warnings med `-std=c17` |
| G-04 | Udviklingsmiljø: Windows 11 host-OS (devcontainer valgfri) | PowerShell-scripts. Valgfri `.devcontainer/` | `scripts/`, `.devcontainer/` | 1 | ✅ | DoD Del 1 testet på Windows 11 |
| G-05 | `prj.conf` styrer Kconfig | Al konfiguration i `prj.conf`, projektindstillinger i `app/Kconfig`, hemmeligheder i `secrets.conf` | `app/prj.conf`, `app/Kconfig` | 1–2 | ✅ | Review |
| G-06 | Devicetree til hardware. Afvigelser i `.overlay` | `app/app.overlay` (coretemp, UART1 GPIO18/19, modem-node `simcom,sim7080` + `autostarts`, power-pladsholder GPIO22, alias `modem`). `app/modbus-rtu.overlay` (RS485 + console). Flash-variant via officiel snippet | `app/*.overlay`, `scripts/build.ps1` | 1–4 | 🟡 | Kontrolleret i genereret `zephyr.dts` (Del 3–4). Mangler: hardwaretest |
| G-07 | ALLE afvigelser (fx GPIO/UART-pins) dokumenteres | Pin-tabel og afvigelser i `docs/hardware.md`, plan i `02-arkitektur.md` §6 | `docs/hardware.md` | 1–4 | 🟡 | Review: hver overlay-ændring har en række i `hardware.md` |
| G-08 | Zephyrs egne API'er (netværk, modem, MQTT, Modbus, logging, PM) | `sensor` ✔, `modbus` (client) ✔, `log` ✔, `hwinfo` ✔, `k_timer`/`k_msgq`/`k_poll` ✔, `modem_cellular` + `cellular` API ✔, PPP/net stack/sockets ✔, `net_mgmt`/conn_mgr ✔, `pm_device` ✔. Senere: `mqtt`, `pm`, nanopb | alle moduler | 2–9 | 🟡 | Review: ingen egne AT-/socket-drivere |
| G-09 | Multithreaded, ca. 1 thread pr. modul (netværk, app-logik, Sparkplug, MQTT) | Tråde: `sensor` ✔, `app_logic` ✔ (Del 3), `network` ✔ (Del 4), `mqtt` ✔ (Del 6), `sparkplug` ✔ (Del 7) | `02-arkitektur.md` §3 | 2–7 | 🟡 | Thread analyzer / `kernel threads` viser trådene |
| G-10 | Delte data beskyttes med mutex/semaphore/message queue | `k_msgq` meas_q/cmd_q ✔, `k_mutex` config ✔, `k_sem` sample_sem ✔, `k_spinlock` stats/state ✔, `k_event` net_events + interne events ✔, `atomic_t` registreringsstatus ✔. Senere: batch/tx/rx-køer, `k_mem_slab` | `02-arkitektur.md` §4 | 2–7 | 🟡 | native_sim: målinger går gennem `meas_q` til `app_logic` |
| G-11 | Modulopdelt, hvert modul egen `.c` og `.h` | 9 moduler + `app_types.h` | `app/src/*` | 2 | ✅ | Skelet bygger. Én `.c/.h` pr. modul |
| G-12 | Mappestruktur efter Micro Technics eksempel | `app.overlay`, `CMakeLists.txt`, `Kconfig.sysbuild`, `prj.conf`, `sample.yaml`, `sysbuild.cmake`, `src/`, `remote/` | `app/` | 1 | ✅ | Review |
| G-13 | LP-core-kode i `remote/` (`boards/esp32c6_devkitc_lpcore.overlay`, `CMakeLists.txt`, `prj.conf`, `src/main_ULP.c`) | Sysbuild `ExternalZephyrProject_Add`, board `esp32c6_devkitc/esp32c6/lpcore` | `app/remote/`, `app/sysbuild.cmake` | 9 | ⬜ | Build af begge images. LP-kerne vækker HP-kerne |
| G-14 | TLS (self-signed CA eller Let's Encrypt/certbot) | Let's Encrypt via certbot (serveren har domænet `zephyra.vps.webdock.cloud`). Mbed TLS 4.1 + PSA | `mqtt_client.c`, `cloud/` | 5–6 | 🟡 | Server: Del 5. Enhed: Del 6, TLS 1.3/1.2, kæde + værtsnavn verificeres mod 4 ISRG-rødder. Testet på native_sim mod Mosquitto 2.0.18 (24/24, inkl. forkert CA/navn/plain). Datotjek fra Del 7 (udløbet og ikke-endnu-gyldigt certifikat afvises, testet). Mangler: hardwaretest |
| G-15 | MQTT | Zephyr MQTT-bibliotek (3.1.1) over TLS-socket | `mqtt_client.c` | 6 | 🟡 | Del 6: `mqtt`-tråd, CONNECT/SUBSCRIBE/PUBLISH, Will, keep-alive, backoff. Testet på native_sim. Mangler: `mosquitto_sub` med rigtig enhed |
| G-16 | Modbus RTU/TCP til dataopsamling | Modbus RTU-klient (Zephyr `modbus`, client) på `uart0` → RS485 (GPIO20/21, DE/RE GPIO23). Registertabel med skalering, Kconfig for slave/baud/paritet/timeout/retries | `modbus.c/.h`, `modbus-rtu.overlay`, `overlay-modbus-rtu.conf` | 3 | 🟡 | Bygget + simuleret (native_sim). Test mod ModbusTools/Modbus Slave via USB-RS485: `03-dataopsamling.md` §5.3 |
| G-17 | Sparkplug B (`spBv1.0`) som beskedstandard | nanopb + Tahu `sparkplug_b.proto`. NBIRTH/DBIRTH/DDATA/NDEATH, bdSeq/seq | `sparkplug.c/.h` | 7 | 🟡 | Del 7: NBIRTH/DBIRTH/DDATA/NDATA/NDEATH (Will + før reboot), bdSeq/seq, aliases, NCMD. Testet på native_sim mod Mosquitto og verificeret med Tahus officielle `.proto` (68/68). Mangler: hardware |
| G-18 | Logging via logging-subsystemet | `CONFIG_LOG`, deferred mode | alle `.c` | 1 | ✅ | Boot-log |
| G-19 | SKAL logge: netværksstatus | `network` logger hver tilstandsovergang, registreringsstatus, IP-adresse, modem-info, DNS/TCP-tjek og periodisk status (RSSI) hvert 60. s | `network.c` | 4 | 🟡 | Forventet log i `04-netvaerk.md` §5.3. Mangler: hardwaretest |
| G-20 | SKAL logge: cloud-forbindelse | `mqtt` logger connect/disconnect/fejl | `mqtt_client.c` | 6 | 🟡 | Logger DNS, TLS-version/ciphersuite, CONNACK-fejl, session-slut, backoff, link-tab, stats. Testet på native_sim |
| G-21 | SKAL logge: errors | `LOG_ERR` ved alle fejlstier (returkoder tjekkes). Del 3: sensorfejl, Modbus timeout/CRC/exception | alle | 1– | 🟡 | Simulerede fejl (`APP_SENSOR_SIM_FAULT_EVERY`) giver `LOG_ERR` (native_sim) |
| G-22 | SKAL logge: custom logs | Device-id, reset-årsag, konfiguration, måling pr. cyklus (`meas #n`), `Batch #n ready` | `device_info.c`, `app_logic.c` | 1–3 | ✅ | Boot-log + målelog |
| G-23 | Log levels korrekt | ERR = fejl, der kræver handling. WRN = afvigelse (fx brownout). INF = tilstandsskift. DBG = detaljer. `CONFIG_APP_LOG_LEVEL` | alle | 1– | 🟡 | Review |
| G-24 | `LOG_MODULE_REGISTER` i hver `.c`-fil | Én pr. fil, unikt modulnavn | alle `.c` | 2 | ✅ | `grep LOG_MODULE_REGISTER app/src/*.c` |
| G-25 | Hver MQTT-besked: device-id, timestamp og aflæste værdier | device-id = edge node id i topic. Sparkplug `timestamp` + pr. metric. Værdier = metrics | `sparkplug.c` | 7 | 🟡 | Topic = edge node id. Payload-timestamp + UTC-timestamp pr. måling (SNTP). Testet: tidsstempler stiger og ligger inden for ±60 s af værtens tid. Mangler: hardware |

### Krav fra Del 3-opgaven (dataopsamling)

| ID | Krav | Hvordan det løses | Modul / fil | Del | Status | Test |
|---|---|---|---|---|---|---|
| D-01 | Måling drevet af `k_timer` (ikke busy-wait) | Periodisk `k_timer`. Expiry (ISR) giver `k_sem`. Tråden blokerer i `k_sem_take()` | `sensor.c` | 3 | ✅ | native_sim: målinger hvert 5,000 s |
| D-02 | Måling = struct med timestamp, værdier og kvalitetsflag | `struct measurement` (uptime_ms, timestamp_ms, seq, values[] med quality, samlet quality) | `app_types.h` | 3 | ✅ | Review + log |
| D-03 | Målingen lægges i en `k_msgq` til andre tråde | `meas_q` (16 × 56 B), ejet af `app_logic`. Forbruges via `k_poll` | `app_logic.c` | 3 | ✅ | native_sim: `app_logic` logger hver måling |
| D-04 | Timeout/CRC-fejl → `LOG_ERR`, retry med backoff, måling markeres ugyldig, programmet fortsætter | Retry pr. request (100/200 ms), cyklus-backoff (0/1/3/7/8), `QUALITY_BAD`/`STALE` | `modbus.c`, `sensor.c` | 3 | 🟡 | Simuleret ✔ (native_sim). Mangler: rigtig RS485 + ModbusTools |
| D-05 | Konfigurerbar slave-adresse, registre, skalering | Kconfig `APP_MODBUS_*` + `reg_table[]` i `modbus.c` | `app/Kconfig`, `modbus.c` | 3 | ✅ | Review |
| D-06 | Kconfig-option med simulerede værdier | `APP_SENSOR_SIMULATION` (+ `APP_SENSOR_SIM_FAULT_EVERY`), `build.ps1 -Simulation` | `sensor.c`, `modbus.c` | 3 | ✅ | native_sim-kørsel |
| D-07 | Testvejledning mod ModbusTools/Modbus Slave via USB-RS485 | `03-dataopsamling.md` §5.3 | docs | 3 | ✅ | – |

## Projekt A-specifikke krav

| ID | Krav | Hvordan det løses | Modul / fil | Del | Status | Test |
|---|---|---|---|---|---|---|
| A-01 | Cellulær forbindelse til cloud via udleveret SIM-kort | M5Stamp CAT-M (SIM7080G), `modem_cellular` (`simcom,sim7080`), CMUX + PPP, APN `onomondo`, forbindelsestjek (DNS + TCP til egen server), backoff, CMUX-recovery | `network.c`, `app.overlay`, `prj.conf` | 4 | 🟡 | Bygget. Hardwaretest: `04-netvaerk.md` §5.4 (IP, DNS, TCP, RESET-recovery, antenne af/på) |
| A-02 | Sikker kommunikation (TLS) | Se G-14. Serverens certifikat verificeres mod CA. Hostname-check (SNI) | `mqtt_client.c` | 6 | 🟡 | Negative tests bestået på native_sim: forkert CA og forkert værtsnavn → `-0x2700`, plain → fejl. Hostname-check via `TLS_HOSTNAME` (SNI sendes ikke). Mangler: hardware |
| A-03 | Lavt strømforbrug via sleep modes eller interrupts | PM light sleep i idle, event-drevne tråde (sensor blokerer på `k_sem` fra `k_timer`, network venter på `k_event`, interrupt-drevet modem-UART), modem PSM/eDRX, deep sleep + LP-kerne | `power.c`, `sensor.c`, `network.c`, `remote/` | 3, 4, 9 | 🟡 | Multimetermåling før/efter (mA) |
| A-04 | Lavt dataforbrug via batching eller små payloads | Batch 6 målinger/DDATA (batching i `app_logic` ✔ Del 3), protobuf, metric aliases, QoS 0, lang keep-alive | `app_logic.c`, `sparkplug.c` | 3, 7, 9 | 🟡 | Bytes/døgn (Onomondo) med og uden batching |
| A-05 | SKAL bruge egen cloud-server til dataopsamling | Webdock VPS `zephyra.vps.webdock.cloud`: Mosquitto + Node-RED | `cloud/` | 5 | 🟡 | Server-opsætning og testpublisher lavet i Del 5. Afventer kørsel på VPS og data fra enheden |
| A-06 | Dataopsamling med fast interval (5–15 s) | `APP_SAMPLE_INTERVAL_MS` (range 5000–15000, standard 10000). Periodisk `k_timer` → `k_sem` → sensor-tråd. Runtime-ændring valideres også (5–15 s) | `sensor.c`, `app_logic.c`, `app/Kconfig` | 2–3 | 🟡 | Kconfig afviser 20000 (Del 2). native_sim: præcis 5,000 s mellem målinger, også efter fejlcyklus. Mangler: hardwaretest |
| A-07 | Data sendes i MQTT som Sparkplug B | Se G-17 | `sparkplug.c` | 7 | 🟡 | Se G-17 |
| A-08 | IEC 62443-4-2 og/eller CRA Annex I (1.c fortrolighed, 1.d integritet) kan redegøres for | Mapping-dokument. Fx CR 4.1 / CRA 1.c (TLS-kryptering), CR 3.1 / CRA 1.d (TLS-integritet, MCUboot), CR 1.x (MQTT-autentifikation) | `docs/08-sikkerhed.md` | 8 | ⬜ | Review |
| A-09 | Mindst 2 krav implementeret/identificeret | Kandidater: kommunikationsfortrolighed (TLS), integritet (TLS + signeret firmware), autentifikation (brugernavn/password → mTLS), ingen hemmeligheder i git | se A-08 | 8 | ⬜ | Test pr. krav |
| A-10 | Relevant security level vurderes | SL-vurdering (SL 1/2) med begrundelse | `docs/08-sikkerhed.md` | 8 | ⬜ | Review |

## Nice to have

| ID | Krav | Hvordan det løses | Modul / fil | Del | Status | Test |
|---|---|---|---|---|---|---|
| N-01 | Kommandoer fra cloud via MQTT/Sparkplug (NCMD) | Rebirth, Reboot, Sample Interval, Batch Size → `cmd_q` → `app_logic` | `sparkplug.c`, `app_logic.c` | 7/10 | 🟡 | Del 7: Rebirth, Reboot, Sample Interval, Batch Size (navn eller alias) + Node-RED-knapper (inject) → NCMD. Ny værdi bekræftes med NDATA. Testet på native_sim. Dashboard-knapper: Del 10 |
| N-02 | OTA | MCUboot (allerede bootloader) + download over MQTT/HTTP | – | 10 | ⬜ | |
| N-03 | Dashboard | Node-RED dashboard på VPS'en | `cloud/node-red/` | 5/10 | 🟡 | Node-RED Dashboard 2.0 med graf + seneste besked (Del 5). Udvides i Del 10 |
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
