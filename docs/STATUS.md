# STATUS – Zephyr_A (Projekt A)

Opdateres i slutningen af hver del. Detaljeret kravstatus: [02-kravmatrix.md](02-kravmatrix.md).

| Del | Emne | Branch | Status |
|---|---|---|---|
| 1 | Udviklingsmiljø Windows 11 + repo-skelet | `del-01-setup` | ✅ Godkendt |
| 2 | Design: arkitektur, tråde, IPC, pin-plan, kravmatrix, modul-skelet | `del-02-design` | ✅ Godkendt |
| 3 | Dataopsamling: intern temperatursensor, Modbus RTU, simulering | `del-03-dataopsamling` | Færdig – **ikke testet på hardware endnu** (bruger gik videre til Del 4) |
| 4 | Cellulært netværk (SIM7080G, CMUX, PPP) | `del-04-netvaerk` | Færdig – **ikke testet på hardware endnu** (bruger gik videre til Del 5) |
| 5 | Cloud-server (Webdock: SSH, Mosquitto, Let's Encrypt, Node-RED) | `del-05-cloud` | Færdig – **ikke kørt på VPS endnu** (bruger gik videre til Del 6) |
| 6 | MQTT + TLS (Mbed TLS 4.1/PSA) | `del-06-mqtt-tls` | Færdig – testet på native_sim, **ikke på hardware endnu** |
| 7 | Sparkplug B + tidssynkronisering | `del-07-sparkplug` | Færdig – testet på native_sim, afventer hardwaretest og godkendelse |
| 8 | Sikkerhed: IEC 62443-4-2 / CRA | – | Ikke startet |
| 9 | Lavt strøm- og dataforbrug, LP-kerne (`remote/`) | – | Ikke startet |
| 10 | Nice to have (NCMD, dashboard, mTLS, MCUboot-signering/OTA) | – | Ikke startet |
| 11 | Afslutning: test, memory layout, rapportmateriale | – | Ikke startet |

Delene blev omnummereret i Del 3 (dataopsamling før netværk).

## Del 7 – lavet

- `timesync.c`: SNTP (2 servere, plausibilitetstjek ≥ 2026), offset til uptime, `SYS_CLOCK_REALTIME` sættes, resync
  hver 24. time. `network` melder først `EVT_UP`, når UTC er gyldig. `sensor` tidsstempler målinger
- TLS: `MBEDTLS_HAVE_TIME_DATE=y`, så certifikaternes gyldighedsdatoer tjekkes
- `sparkplug.c` (nanopb, callbacks): NBIRTH (10 metrics), DBIRTH (alias 100+, Float °C), DDATA pr. batch med
  UTC pr. måling (`is_null` ved dårlig kvalitet, automatisk opdeling over 512 B), NDATA (config + status hvert 15. min),
  NDEATH som Will og før reboot, bdSeq/seq efter Sparkplug B 3.0, NCMD Rebirth/Reboot/Sample Interval/Batch Size
- `mqtt_client.c`: kasserer beskeder fra forrige session, `sparkplug_on_disconnected()`. Selvtesten er fjernet
- `app_logic.c`: batches → sparkplug, konfigurationsændringer bekræftes med NDATA
- `app/proto/` + genereret `app/src/proto/sparkplug_b.pb.c/h` (committet), `scripts/gen_sparkplug_pb.py`
- Node-RED: NCMD-knapper + encoder (`ncmd.js`), dekoder husker edge nodes, grafen viser kun procesværdier + RSSI
- `docs/07-sparkplug-tid.md`

### Verificeret

- Bygger uden warnings: standard (FLASH 10,3 %, RAM 39,4 %), `-TlsDebug`, `-ModbusRtu -Simulation`, uden secrets
- native_sim mod Mosquitto 2.0.18 + lokal SNTP: **68/68** (hel Sparkplug-session kontrolleret med uafhængig dekoder,
  NCMD via navn og alias, rebirth, reboot → NDEATH, link-tab → Will-NDEATH + bdSeq+1, udløbet/fremtidigt
  certifikat afvist, SNTP-svar fra 1994 afvist). Beskederne er også parset med Tahus officielle `.proto` (protoc)
- Node-RED-funktioner: `decode.test.js` og `ncmd.test.js` bestået
- Mosquitto sender den gamle Will ved "client id takeover" (verificeret lokalt)

### Mangler (hardware/server)

- [ ] Del 7-tjeklisten i `07-sparkplug-tid.md` §11 (kræver Del 5 og Del 6)
- [ ] Del 3–6 på hardware/VPS

## Del 6 – lavet

- `mqtt_client.c`: `mqtt`-tråd med tilstandsmaskine (IDLE → RESOLVING → CONNECTING → CONNECTED, BACKOFF), MQTT 3.1.1
  over TLS, brugernavn = enheds-id, password fra `secrets.conf`, Last Will (klar til NDEATH), subscribe NCMD/DCMD
  (QoS 1, PUBACK), publish-kø (`k_msgq` + `k_mem_slab`), eventfd-wakeup i `zsock_poll()` (ingen polling), keep-alive
  med PINGRESP-timeout, link-tab via `NET_EVENT_L4_DISCONNECTED`, backoff 10–600 s med jitter (permanente fejl → max),
  statistik
- TLS: Mbed TLS 4.1 + PSA, TLS 1.3 (1.2 som reserve), ECDSA P-256/P-384 og RSA, 4 ISRG-rødder (YE, YR, X2, X1) som DER
  i flash (`app/ca/`, `tls_ca.c`), værtsnavn-tjek, input 8 kB / output 2 kB, heap 56 kB
- `mqtt_selftest.c`: Sparkplug B NDATA (Test/Uptime, Test/RSSI, Test/MQTT Connects) hvert minut (fjernes i Del 7)
- `build.ps1`: `app\secrets.conf` tilføjes automatisk, ny `-TlsDebug`
- `app/tests/mqtt_tls/`: test på native_sim mod Mosquitto 2.0.18 med Let's Encrypt-lignende kæder
- `decode.js`/`flows.json`: modtagetid, når en besked ikke har tidsstempel
- `docs/06-mqtt-tls.md`

### Verificeret

- Bygger uden compiler-warnings: standard (FLASH 8,7 %, RAM 37,4 %), `-TlsDebug`, `-ModemDebug -TlsDebug`,
  `-ModbusRtu -Simulation`, og uden `secrets.conf`
- native_sim mod Mosquitto 2.0.18: **24/24** (TLS 1.3 med ECDSA- og RSA-kæde, TLS 1.2 med ECDSA, data + NCMD,
  link-tab, broker-genstart, afvisning af forkert CA, forkert værtsnavn, plain MQTT og forkert password)
- Målt: Mbed TLS-heap peak 32 kB (ECDSA) / 37 kB (RSA). Ny forbindelse ca. 0,7 kB op / 3,9–4,8 kB ned. NDATA 170 B
- Fund: RSA-PSS kræver `MBEDTLS_X509_RSASSA_PSS_SUPPORT` + `PSA_WANT_ALG_RSA_OAEP`. For lille heap giver `-0x2700`
  "not trusted". TLS 1.2 + RSA mod OpenSSL virker ikke i Mbed TLS 4.1 (TLS 1.3 bruges)

### Mangler (hardware/server)

- [ ] Del 6-tjeklisten i `06-mqtt-tls.md` §10 (kræver Del 5 på serveren)
- [ ] Stack-forbrug og handshake-tid fra loggen sendes til Claude
- [ ] Del 5-tjekliste på VPS'en, Del 3- og Del 4-tests på hardware

## Del 5 – lavet

- `cloud/scripts/` (bash, kan køres igen): 10-base (pakker, auto-opdateringer, NTP), 20-firewall (UFW 22/80/443/1880/8883,
  FTP lukket), 30-certs (Webdocks Let's Encrypt-cert + deploy-hook), 40-mosquitto, 50-nodered, 60-add-device, 90-verify
- Mosquitto: TLS ≥ 1.2 på 8883, 1883 kun på localhost, ingen anonyme, password pr. enhed, ACL med `pattern ... %u`
  (brugernavn = Sparkplug edge node-id)
- Let's Encrypt deploy-hook kopierer fornyede certifikater til Mosquitto og Node-RED med rigtige rettigheder
- Node-RED 5 + Dashboard 2.0: HTTPS, login til editor og dashboard (bcrypt), `credentialSecret`, flow med
  Sparkplug B-dekoder (aliaser, batch-tidsstempler), datalog (JSON Lines pr. dag), graf og seneste besked
- `cloud/tools/spb-test-publisher.js`: simuleret edge node (TLS-verifikation, NBIRTH/DBIRTH/DDATA-batch, NDEATH som will)
- `docs/05-cloud.md`: trin for trin (SSH-nøgle fra Windows, passwordskift, scripts, Node-RED-import, test), sikkerhedsvalg,
  fejlsøgning og vedligehold
- Beslutning til Del 7: metric-værdier sendes som Float i °C

### Verificeret (i udviklingsmiljøet, ikke på VPS'en)

- Mosquitto 2.0.18 med repoets config/ACL: anonym/forkert password afvist, TLS 1.1 og plain afvist, ACL forhindrer
  forfalskning og aflytning, Last Will NDEATH leveres, 1883 kun på 127.0.0.1
- Fund: nøgle skal være `root:mosquitto 0640`, passwd/acl `mosquitto 0600` (Mosquitto skifter bruger før indlæsning)
- Deploy-hook, Node-RED-settings (med rigtig bcrypt-hash) og `decode.js` (unit-test) afprøvet. ShellCheck uden fund
- **Ikke afprøvet:** import af `flows.json` i en kørende Node-RED (npm ikke tilgængeligt herfra)

### Mangler (på serveren)

- [ ] Del 5-tjeklisten i `05-cloud.md` §4 (SSH-nøgle, passwords skiftet, `90-verify.sh`, testpublisher → dashboard)
- [ ] Certifikatkæden fra `30-certs.sh` sendes til Claude (bekræfter nøgletype og kæde; enheden stoler fra Del 6 på alle fire ISRG-rødder)
- [ ] Del 3- og Del 4-tests på hardware

## Del 4 – lavet

- `app.overlay`: `uart1` på GPIO18/19 (115200), modem-node `simcom,sim7080` med `autostarts`, `mdm-power-gpios`-pladsholder
  GPIO22 (ikke forbundet), alias `modem`
- `prj.conf`: `modem_cellular`, APN `onomondo`, dedikeret modem-workqueue, PPP, IPv4/TCP/UDP, sockets, DNS (2 servere,
  10 s timeout), connection manager, heap 4 kB til `getaddrinfo`
- `network.c`: tråd med tilstandsmaskine (off → starting → registering → ppp-up → connected, backoff 30 s … 15 min),
  net_mgmt- og cellular-callbacks → `k_event`, log af alle tilstandsskift, registrering, IP, modem-info, RSSI hvert
  60. s, forbindelsestjek (DNS + TCP :80 til `zephyra.vps.webdock.cloud`), offentlige events `NETWORK_EVT_UP/DOWN`
- CMUX-recovery: CLD-ramme før modemstart, fordi modemet beholder strømmen ved ESP-reset og ellers kan hænge i CMUX
- `build.ps1 -ModemDebug` (`overlay-modem-debug.conf`): shell med `modem at AT...`, `net iface`, `net dns query`,
  debug-log fra CMUX/PPP
- `docs/04-netvaerk.md`: design, fund i driveren, konfiguration, test og fejlsøgning
- **Rettelse:** Del 3 havde ved en fejl fjernet Projekt A-, nice-to-have-, out-of-scope- og procesafsnittene fra
  `02-kravmatrix.md`. De er genskabt og opdateret

### Verificeret

- Bygger uden compiler-warnings: standard (FLASH 5,1 %, RAM 20,3 %), `-ModemDebug`, `-ModbusRtu` og `-ModbusRtu -Simulation`
- Devicetree kontrolleret i `zephyr.dts` (uart1-pins, modem-node med `autostarts` og GPIO22)
- Driverens opførsel uden PWRKEY gennemgået i `modem_cellular.c` (autostarts, suspend, init-fejl)
- CMUX CLD-rammens FCS-algoritme verificeret mod referencerammer
- Fundet og rettet før test: `HEAP_MEM_POOL_SIZE=0` ville få alle DNS-opslag til at fejle

### Mangler (test på hardware)

- [ ] Del 4-tjeklisten i `04-netvaerk.md` §5.4 (IP, DNS, TCP, status-log, RESET-recovery, antenne af/på, ingen brownout)
- [ ] Del 3-tests (`03-dataopsamling.md` §5.1–5.2), der ikke er kørt endnu

## Del 3 – lavet

- `sensor`-tråd: periodisk `k_timer` → `k_sem` → måling (ingen busy-wait), interval fra Kconfig
  (standard 10 s, range 5–15 s) og ændrbart mens programmet kører (genstarter timeren)
- Intern temperatursensor via Zephyrs sensor-API (`&coretemp` aktiveret i `app.overlay`)
- `struct measurement`: uptime-tidsstempel, UTC-felt (Del 7), seq, værdier pr. kanal med kvalitetsflag
  (GOOD/BAD/STALE) og samlet kvalitet
- `app_logic`-tråd: forbruger `meas_q` (`k_msgq`) og `cmd_q` via `k_poll`, logger hver måling og samler
  batches (6). Kommandoer til interval/batch-størrelse valideres
- Modbus RTU-klient (Zephyr `modbus`, client) med registertabel og skalering, Kconfig for
  slave/baud/paritet/timeout/retries, retry med eksponentiel backoff og cyklus-backoff
- Simulering (`APP_SENSOR_SIMULATION`) med valgfri fejlinjektion
- `build.ps1 -Simulation` / `-ModbusRtu` (kan kombineres) og automatisk pristine build ved variantskift
- `docs/03-dataopsamling.md` med testvejledning (ModbusTools / Modbus Slave via USB-RS485)

### Verificeret

- Fire varianter bygger med sysbuild + 4 MB uden compiler-warnings: standard (FLASH 3,3 %, RAM 12,4 %),
  simulering, Modbus RTU og Modbus RTU + simulering
- Modbus-variantens devicetree er kontrolleret i `zephyr.dts`: `uart0` på GPIO20/21, `modbus0`, console på `usb_serial`
- Simuleringen er kørt på `native_sim`: interval præcis 5,000 s (også efter en fejlcyklus på 670 ms),
  batching, `LOG_ERR` ved fejl, retry 100/200 ms, `BAD`/`STALE` og cyklus-backoff 0/1/3/7/8

### Mangler (test på hardware)

- [ ] Standard-build på boardet: `Temperature/Die` hver 10. s, `Batch #n ready` for hver 6. måling
- [ ] `-ModbusRtu -Simulation` på boardet: log på USB-porten, fejl hver 5. cyklus
- [ ] (Når RS485-hardware er klar) `-ModbusRtu` mod ModbusTools/Modbus Slave, inkl. fejltests

## Kendte problemer / noter

- **Mbed TLS 4.1 (PSA):** løst i Del 6 (se `06-mqtt-tls.md` §7). Certifikatdatoer tjekkes fra Del 7.
- **bdSeq** gemmes ikke i flash og starter ved 0 efter reboot (`07-sparkplug-tid.md` §9).
- **RNG:** ESP32-C6's hardware-RNG har kun fuld entropi med aktiv radio. Vurderes i Del 8.
- **MQTT-password** ligger i klartekst i flash. Beskyttes i Del 8.
- **`CONFIG_PM` + logging:** linkerfejl `log_const_soc` i Zephyr 4.4.0. Workaround verificeret
  (registrér log-modulet `soc` i app'en). Indføres i del 9.
- **Modem uden PWRKEY:** `simcom,sim7080` kræver `mdm-power-gpios`. Pladsholder på GPIO22
  (uforbundet) + `autostarts`, og CMUX-recovery før start. Testes på hardware.
- **Ingen RTS/CTS** til modemet: risiko for tabte bytes under PPP. Holdes øje med i hardwaretesten.
- Sysbuild giver en harmløs Kconfig-warning om `MCUBOOT_UPDATE_FOOTER_SIZE` (kun relevant for OTA).
- Modbus RTU-varianten flytter console til USB-porten (USB Serial/JTAG). Den rigtige Modbus-kilde/slave er
  stadig ikke kendt. Registertabellen i `modbus.c` er et eksempel (holding 0–1, 0,1-skalering).
- **Webdock:** passwords til admin/MySQL/FTP var synlige på et skærmbillede. Skal skiftes (05-cloud.md §3.1).
- Pc'en har ældre installationer (`C:\zephyrproject`, `C:\Users\paulm\zephyrproject`). De bruges ikke.

## Del 2 – resumé

Arkitektur (`02-arkitektur.md`), kravmatrix, modul-skelet og `app/Kconfig`. `west.yml` + `tf-psa-crypto`.
Testet på hardware 2026-11-02.

## Del 1 – resumé

Windows 11-miljø (`setup/build/flash/monitor.ps1`), Zephyr 4.4.0 + SDK 1.0.1, T2-manifest, sysbuild +
MCUboot, 4 MB flash-snippet. Testet på hardware 2026-11-02 (COM3, device-id `esp32c6-14c19fe0ab24`).
