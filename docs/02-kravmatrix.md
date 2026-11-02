# Kravmatrix – Projekt A

Alle krav fra kravspecifikationen (generelle krav, Projekt A-specifikke krav, nice to have og
out of scope) samt arbejdsreglerne for projektet. Matrixen opdateres i slutningen af hver del.

**Status:** ✅ opfyldt og testet · 🟡 delvist / design klar · ⬜ ikke startet · ➖ out of scope

**Plan for delene** (omnummereret i Del 3):
1 Setup · 2 Design · 3 Dataopsamling · 4 Cellulært netværk · 5 Cloud-server · 6 MQTT + TLS ·
7 Sparkplug B + tid · 8 Sikkerhed (IEC 62443-4-2 / CRA) · 9 Lavt strøm/data + LP-kerne ·
10 Nice to have · 11 Afslutning

*Sidst opdateret: Del 3.*

## Generelle krav

| ID | Krav | Hvordan det løses | Modul / fil | Del | Status | Test |
|---|---|---|---|---|---|---|
| G-01 | Zephyr 4.4.0 | `west.yml` pinner `zephyr` til `v4.4.0` (T2-manifest) | `west.yml` | 1 | ✅ | `west list` viser v4.4.0. Boot-log: `Zephyr version: 4.4.0` |
| G-02 | Zephyr SDK der matcher 4.4.0 | SDK-version læst fra `zephyr/SDK_VERSION` = 1.0.1. Kun `riscv64-zephyr-elf` | `scripts/setup.ps1` | 1 | ✅ | `setup.ps1` finder SDK 1.0.1. Build OK |
| G-03 | Kompatibel med min. C17 | `CONFIG_STD_C17=y` (compiler kører `-std=c17`) | `app/prj.conf` | 1 | ✅ | Build uden warnings med `-std=c17` |
| G-04 | Udviklingsmiljø: Windows 11 host-OS (devcontainer valgfri) | PowerShell-scripts. Valgfri `.devcontainer/` | `scripts/`, `.devcontainer/` | 1 | ✅ | DoD Del 1 testet på Windows 11 |
| G-05 | `prj.conf` styrer Kconfig | Al konfiguration i `prj.conf`, projektindstillinger i `app/Kconfig`, hemmeligheder i `secrets.conf` | `app/prj.conf`, `app/Kconfig` | 1–2 | ✅ | Review |
| G-06 | Devicetree til hardware. Afvigelser i `.overlay` | `app/app.overlay` (coretemp ✔, UART1/modem i Del 4). `app/modbus-rtu.overlay` (RS485 + console). Flash-variant via officiel snippet | `app/*.overlay`, `scripts/build.ps1` | 1–4 | 🟡 | Coretemp + Modbus-overlay bygget og kontrolleret i `zephyr.dts` (Del 3). Modem i Del 4 |
| G-07 | ALLE afvigelser (fx GPIO/UART-pins) dokumenteres | Pin-tabel og afvigelser i `docs/hardware.md`, plan i `02-arkitektur.md` §6 | `docs/hardware.md` | 1–4 | 🟡 | Review: hver overlay-ændring har en række i `hardware.md` |
| G-08 | Zephyrs egne API'er (netværk, modem, MQTT, Modbus, logging, PM) | `sensor` ✔, `modbus` (client) ✔, `log` ✔, `hwinfo` ✔, `k_timer`/`k_msgq`/`k_poll` ✔. Senere: `modem_cellular`, PPP, `mqtt`, `pm`, nanopb | alle moduler | 2–9 | 🟡 | Review: ingen egne AT-/socket-drivere |
| G-09 | Multithreaded, ca. 1 thread pr. modul (netværk, app-logik, Sparkplug, MQTT) | Tråde: `sensor` ✔, `app_logic` ✔ (Del 3). `network`, `mqtt`, `sparkplug` følger | `02-arkitektur.md` §3 | 2–7 | 🟡 | Thread analyzer / `kernel threads` viser trådene |
| G-10 | Delte data beskyttes med mutex/semaphore/message queue | `k_msgq` meas_q/cmd_q ✔, `k_mutex` config ✔, `k_sem` sample_sem ✔, `k_spinlock` stats ✔. Senere: batch/tx/rx-køer, `k_event`, `k_mem_slab` | `02-arkitektur.md` §4 | 2–7 | 🟡 | native_sim: målinger går gennem `meas_q` til `app_logic` |
| G-11 | Modulopdelt, hvert modul egen `.c` og `.h` | 9 moduler + `app_types.h` | `app/src/*` | 2 | ✅ | Skelet bygger. Én `.c/.h` pr. modul |
| G-12 | Mappestruktur efter Micro Technics eksempel | `app.overlay`, `CMakeLists.txt`, `Kconfig.sysbuild`, `prj.conf`, `sample.yaml`, `sysbuild.cmake`, `src/`, `remote/` | `app/` | 1 | ✅ | Review |
| G-13 | LP-core-kode i `remote/` (`boards/esp32c6_devkitc_lpcore.overlay`, `CMakeLists.txt`, `prj.conf`, `src/main_ULP.c`) | Sysbuild `ExternalZephyrProject_Add`, board `esp32c6_devkitc/esp32c6/lpcore` | `app/remote/`, `app/sysbuild.cmake` | 9 | ⬜ | Build af begge images. LP-kerne vækker HP-kerne |
| G-14 | TLS (self-signed CA eller Let's Encrypt/certbot) | Let's Encrypt via certbot (serveren har domænet `zephyra.vps.webdock.cloud`). Mbed TLS 4.1 + PSA | `mqtt_client.c`, `cloud/` | 5–6 | ⬜ | Broker afviser plain MQTT. Enhed forbinder kun med gyldigt cert |
| G-15 | MQTT | Zephyr MQTT-bibliotek (3.1.1) over TLS-socket | `mqtt_client.c` | 6 | ⬜ | Beskeder ses på brokeren (`mosquitto_sub`) |
| G-16 | Modbus RTU/TCP til dataopsamling | Modbus RTU-klient (Zephyr `modbus`, client) på `uart0` → RS485 (GPIO20/21, DE/RE GPIO23). Registertabel med skalering, Kconfig for slave/baud/paritet/timeout/retries | `modbus.c/.h`, `modbus-rtu.overlay`, `overlay-modbus-rtu.conf` | 3 | 🟡 | Bygget + simuleret (native_sim). Test mod ModbusTools/Modbus Slave via USB-RS485: `03-dataopsamling.md` §5.3 |
| G-17 | Sparkplug B (`spBv1.0`) som beskedstandard | nanopb + Tahu `sparkplug_b.proto`. NBIRTH/DBIRTH/DDATA/NDEATH, bdSeq/seq | `sparkplug.c/.h` | 7 | 🟡 | Topic-builder i skelet. Decode i Node-RED/Sparkplug-værktøj |
| G-18 | Logging via logging-subsystemet | `CONFIG_LOG`, deferred mode | alle `.c` | 1 | ✅ | Boot-log |
| G-19 | SKAL logge: netværksstatus | `network` logger hver tilstandsovergang | `network.c` | 4 | ⬜ | Log viser `starting → registering → ppp-up → connected` |
| G-20 | SKAL logge: cloud-forbindelse | `mqtt` logger connect/disconnect/fejl | `mqtt_client.c` | 6 | ⬜ | Log ved CONNACK og afbrydelse |
| G-21 | SKAL logge: errors | `LOG_ERR` ved alle fejlstier (returkoder tjekkes). Del 3: sensorfejl, Modbus timeout/CRC/exception | alle | 1– | 🟡 | Simulerede fejl (`APP_SENSOR_SIM_FAULT_EVERY`) giver `LOG_ERR` (native_sim) |
| G-22 | SKAL logge: custom logs | Device-id, reset-årsag, konfiguration, måling pr. cyklus (`meas #n`), `Batch #n ready` | `device_info.c`, `app_logic.c` | 1–3 | ✅ | Boot-log + målelog |
| G-23 | Log levels korrekt | ERR = fejl, der kræver handling. WRN = afvigelse (fx brownout). INF = tilstandsskift. DBG = detaljer. `CONFIG_APP_LOG_LEVEL` | alle | 1– | 🟡 | Review |
| G-24 | `LOG_MODULE_REGISTER` i hver `.c`-fil | Én pr. fil, unikt modulnavn | alle `.c` | 2 | ✅ | `grep LOG_MODULE_REGISTER app/src/*.c` |
| G-25 | Hver MQTT-besked: device-id, timestamp og aflæste værdier | device-id = edge node id i topic. Sparkplug `timestamp` + pr. metric. Værdier = metrics | `sparkplug.c` | 7 | 🟡 | Decode af DDATA viser alle tre |

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
