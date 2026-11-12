# Kravmatrix – Projekt A

Alle krav fra kravspecifikationen (generelle krav, Projekt A-specifikke krav, nice to have og
out of scope) samt arbejdsreglerne for projektet. Matrixen opdateres i slutningen af hver del.

**Status:** ✅ opfyldt og testet · 🟡 delvist / design klar · ⬜ ikke startet · ➖ out of scope

**Plan for delene** (omnummereret i Del 3):
1 Setup · 2 Design · 3 Dataopsamling · 4 Cellulært netværk · 5 Cloud-server · 6 MQTT + TLS ·
7 Sparkplug B + tid · 8 Sikkerhed (IEC 62443-4-2 / CRA) · 9 Lavt strøm/data + LP-kerne ·
10 Nice to have · 11 Afslutning

*Sidst opdateret: Del 11.*

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
| G-08 | Zephyrs egne API'er (netværk, modem, MQTT, Modbus, logging, PM) | `sensor` ✔, `modbus` (client) ✔, `log` ✔, `hwinfo` ✔, `k_timer`/`k_msgq`/`k_poll` ✔, `modem_cellular` + `cellular` API ✔, PPP/net stack/sockets ✔, `net_mgmt`/conn_mgr ✔, `pm_device` ✔. `mqtt` ✔, nanopb ✔, `pm` (light sleep, `pm_policy_state_lock`) ✔, `sys_poweroff` ✔, ULP LP-core ✔ (Del 9), `task_wdt` + `watchdog` ✔ (Del 10) | alle moduler | 2–9 | 🟡 | Review: ingen egne AT-/socket-drivere |
| G-09 | Multithreaded, ca. 1 thread pr. modul (netværk, app-logik, Sparkplug, MQTT) | Tråde: `sensor` ✔, `app_logic` ✔ (Del 3), `network` ✔ (Del 4), `mqtt` ✔ (Del 6), `sparkplug` ✔ (Del 7), `uplink` ✔ (Del 9, kun interval-tilstand). Plus LP-kernen som separat program | `02-arkitektur.md` §3 | 2–9 | 🟡 | Thread analyzer / `kernel threads` viser trådene |
| G-10 | Delte data beskyttes med mutex/semaphore/message queue | `k_msgq` meas_q/cmd_q ✔, `k_mutex` config ✔, `k_sem` sample_sem ✔, `k_spinlock` stats/state ✔, `k_event` net_events + interne events ✔, `atomic_t` registreringsstatus ✔. Senere: batch/tx/rx-køer, `k_mem_slab` | `02-arkitektur.md` §4 | 2–7 | 🟡 | native_sim: målinger går gennem `meas_q` til `app_logic` |
| G-11 | Modulopdelt, hvert modul egen `.c` og `.h` | 12 moduler (`device_info`, `timesync`, `sensor`, `modbus`, `app_logic`, `sparkplug`, `mqtt_client`, `tls_ca`, `network`, `power`, `uplink`, `security`, `health`) + `app_types.h` + `common/lp_shared.h` (delt med LP-kernen) | `app/src/*` | 2–9 | ✅ | Én `.c/.h` pr. modul |
| G-12 | Mappestruktur efter Micro Technics eksempel | `app.overlay`, `CMakeLists.txt`, `Kconfig.sysbuild`, `prj.conf`, `sample.yaml`, `sysbuild.cmake`, `src/`, `remote/` | `app/` | 1 | ✅ | Review |
| G-13 | LP-core-kode i `remote/` (`boards/esp32c6_devkitc_lpcore.overlay`, `CMakeLists.txt`, `prj.conf`, `src/main_ULP.c`) | Sysbuild `ExternalZephyrProject_Add` (`app/sysbuild.cmake`, `Kconfig.sysbuild`), board `esp32c6_devkitc/esp32c6/lpcore`. LP-kernen kører hvert sekund: vækker HP-kernen fra deep sleep og nulstiller den, hvis målingerne står stille i 120 s. Delt hukommelse + logik i `common/lp_shared.h`. LP-billedets SHA-256 tjekkes af det signerede HP-billede | `app/remote/`, `app/common/`, `power.c` | 9 | 🟡 | Bygger (3 images, LP 1577 B). Unittest af LP-logikken 7/7. Mangler: hardwaretest `09-strom-data.md` §6.2 (hæng-test, deep sleep 10 min) |
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
| A-03 | Lavt strømforbrug via sleep modes eller interrupts | Event-drevne tråde og interrupt-drevne UART'er (Del 3–6). **Del 9:** light sleep (`CONFIG_PM`, låst kun under UART-trafik), interval-uplink (`-LowPower`: modem kun tændt ca. 2 % af tiden, ellers `AT+CFUN=0`), PSM slået fra (ingen PWRKEY), eDRX valgfri, deep sleep med LP-kerne-vækning (NCMD Sleep) | `power.c`, `uplink.c`, `network.c`, `sensor.c`, `remote/` | 3, 4, 9 | 🟡 | native_sim: interval-forløbet (scenarie 12). Mangler: multimetermåling, tabel i `09-strom-data.md` §6.3 |
| A-04 | Lavt dataforbrug via batching eller små payloads | Batch 6/12 målinger pr. DDATA, protobuf, metric aliases, QoS 0, lang keep-alive, ren DISCONNECT (ingen Will-trafik). Målt pr. uplink: 0,8 kB op + 3,9 kB ned (TLS) + payload. Beregnet: continuous ca. 0,43 MB/døgn, `-LowPower` ca. 0,62 MB/døgn (uden batching ca. 1,8 MB) | `app_logic.c`, `sparkplug.c`, `uplink.c` | 3, 7, 9 | 🟡 | Byte-tællende proxy (scenarie 13). Mangler: Onomondo-forbrug efter et døgn i hver tilstand |
| A-05 | SKAL bruge egen cloud-server til dataopsamling | Webdock VPS `zephyra.vps.webdock.cloud`: Mosquitto + Node-RED | `cloud/` | 5 | 🟡 | Server-opsætning og testpublisher lavet i Del 5. Afventer kørsel på VPS og data fra enheden |
| A-06 | Dataopsamling med fast interval (5–15 s) | `APP_SAMPLE_INTERVAL_MS` (range 5000–15000, standard 10000). Periodisk `k_timer` → `k_sem` → sensor-tråd. Runtime-ændring valideres også (5–15 s) | `sensor.c`, `app_logic.c`, `app/Kconfig` | 2–3 | 🟡 | Kconfig afviser 20000 (Del 2). native_sim: præcis 5,000 s mellem målinger, også efter fejlcyklus. Mangler: hardwaretest |
| A-07 | Data sendes i MQTT som Sparkplug B | Se G-17 | `sparkplug.c` | 7 | 🟡 | Se G-17 |
| A-08 | IEC 62443-4-2 og/eller CRA Annex I (1.c fortrolighed, 1.d integritet) kan redegøres for | Zoner/conduits, STRIDE, mapping af 27 CR/EDR til IEC 62443-4-2 og CRA bilag I del I (2)(a)–(m) + del II. Kursets "1.c/1.d" = vedtaget forordnings (2)(e) fortrolighed / (2)(f) integritet | `docs/08-sikkerhed.md`, `SECURITY.md` | 8 | 🟡 | Dokument skrevet (Del 8). Afventer review |
| A-09 | Mindst 2 krav implementeret/identificeret | CR 3.1/4.1 TLS 1.3, CR 1.2/1.9 gensidig autentifikation, **CR 3.4 signeret firmware (MCUboot ECDSA P-256, slot0-validering, downgrade-spærring)**, CR 2.1 ACL, CR 3.5 inputvalidering, CR 7.1 reboot-spærring, CR 2.8 `SECURITY:`-logs, CR 4.3 SAR-entropi, CR 7.8 SBOM + sikkerhedsstatus | `security.c`, `sysbuild*`, `sparkplug.c`, `build.ps1` | 6–8 | 🟡 | native_sim 75/75 (negative TLS-tests, reboot-spærring, ingen hemmeligheder i logs). `imgtool verify`: forkert nøgle og ændret byte afvist. Mangler: hardwaretest `08-sikkerhed.md` §6.1 |
| A-10 | Relevant security level vurderes | SL-T 2 for conduit enhed↔broker og cloud, SL-T 1 fysisk (dev-board). SL-C pr. zone med begrundelse og gaps (eFuse-hærdning, fail2ban) | `docs/08-sikkerhed.md` §3 | 8 | 🟡 | Afventer review |

## Nice to have

| ID | Krav | Hvordan det løses | Modul / fil | Del | Status | Test |
|---|---|---|---|---|---|---|
| N-01 | Kommandoer fra cloud via MQTT/Sparkplug (NCMD) | Rebirth, Reboot, Sleep (Del 9), Sample Interval, Batch Size → `cmd_q` → `app_logic`. Node-RED sætter kommandoer til offline noder i kø og sender dem ved næste NBIRTH (Del 9) | `sparkplug.c`, `app_logic.c`, `ncmd.js` | 7/9/10 | 🟡 | Del 7: Rebirth, Reboot, Sample Interval, Batch Size (navn eller alias) + Node-RED-knapper (inject) → NCMD. Ny værdi bekræftes med NDATA. Del 9: Sleep afvist uden LP-kerne (native_sim), kø testet i `ncmd.test.js`. Dashboard-knapper: Del 10 |
| N-02 | OTA | MCUboot (allerede bootloader, signeret, downgrade-spærring) + download over MQTT/HTTPS | – | – | ⬜ | **Udskudt** (valgt i Del 10): størst risiko og kan ikke testes uden hardware. Forudsætningerne (signering, MCUboot, swap-partitioner) er på plads fra Del 8 |
| N-03 | Dashboard | Node-RED Dashboard 2.0: graf + seneste besked (Del 5), **Del 10:** tabel pr. edge node (status, sidste data, FW, reset-årsag, sikkerhed, RSSI, forbindelser, konfiguration, kommandoer i kø) + knapper/sliders til NCMD med samme grænser som firmwaren | `cloud/node-red/overview.js`, `dashcmd.js`, `flows.json` | 5/10 | 🟡 | Unittests `overview`/`dashcmd`/`flows` bestået. Nodeskemaer efter Dashboard 2.0 v1.36. Mangler: import i Node-RED på VPS'en |
| N-04 | mTLS | Egen device-CA på pc'en (`scripts/device_pki.py`, ECDSA P-256), certifikat pr. enhed (CN = enheds-id), `build.ps1 -Mtls`. Mosquitto-listener 8884: `require_certificate`, `use_identity_as_username`, `crlfile` (`65-mtls.sh`). Firmware tjekker CN mod eget id | `tls_ca.c`, `mqtt_client.c`, `cloud/mosquitto/zephyr_a_mtls.conf` | 10 | 🟡 | native_sim mod Mosquitto: forbindelse, ACL via CN, spærret certifikat afvist, forkert certifikat opdaget, enhed uden certifikat afvist. +1,5 kB pr. handshake. Mangler: VPS + hardware |
| N-05 | Bootloader-verifikation (MCUboot) | `SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256`, egen nøgle i `certs\` (gitignored, `new-signing-key.ps1`), `BOOT_VALIDATE_SLOT0=y`, downgrade-spærring. ROM secure boot v2 kun beskrevet (irreversibel eFuse) | `app/sysbuild.conf`, `app/sysbuild/mcuboot.conf`, `build.ps1` | 8 | 🟡 | `imgtool verify` OK, forkert nøgle/ændret byte afvist. Hardware: app signeret med anden nøgle må ikke starte (`08-sikkerhed.md` §6.1) |
| N-06 | Memory layout-diagram | `scripts/memory_layout.py` genererer flash-partitioner, HP-SRAM (sektioner, største objekter) og LP-SRAM fra devicetree, linker-map og ELF. `--check` håndhæver grænser (slot ≤ 50 %, SRAM ≤ 75 %, LP ≤ 50 %). Overlay til stack-måling på hardware | `docs/11-afslutning.md` §2, `app/overlay-stack-usage.conf` | 11 | ✅ | Genereret fra standard-build: 24 % af slot0, 42 % SRAM, LP 38 %. Stack-tal fra hardware udfyldes i §2.2 |

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
