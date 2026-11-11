# STATUS – Zephyr_A (Projekt A)

Opdateres i slutningen af hver del. Detaljeret kravstatus: [02-kravmatrix.md](02-kravmatrix.md).

| Del | Emne | Branch | Status |
|---|---|---|---|
| 1 | Udviklingsmiljø Windows 11 + repo-skelet | `del-01-setup` | ✅ Godkendt |
| 2 | Design: arkitektur, tråde, IPC, pin-plan, kravmatrix, modul-skelet | `del-02-design` | ✅ Godkendt |
| 3 | Dataopsamling: intern temperatursensor, Modbus RTU, simulering | `del-03-dataopsamling` | Færdig – **ikke testet på hardware endnu** (bruger gik videre til Del 4) |
| 4 | Cellulært netværk (SIM7080G, CMUX, PPP) | `del-04-netvaerk` | Færdig – **ikke testet på hardware endnu** (bruger gik videre til Del 5) |
| 5 | Cloud-server (Webdock: SSH, Mosquitto, Let's Encrypt, Node-RED) | `del-05-cloud` | Færdig – afventer kørsel på VPS og godkendelse |
| 6 | MQTT + TLS (Mbed TLS 4.1/PSA) | – | Ikke startet |
| 7 | Sparkplug B (nanopb) + tidssynkronisering (UTC-tidsstempler) | – | Ikke startet |
| 8 | Sikkerhed: IEC 62443-4-2 / CRA | – | Ikke startet |
| 9 | Lavt strøm- og dataforbrug, LP-kerne (`remote/`) | – | Ikke startet |
| 10 | Nice to have (NCMD, dashboard, mTLS, MCUboot-signering/OTA) | – | Ikke startet |
| 11 | Afslutning: test, memory layout, rapportmateriale | – | Ikke startet |

Delene blev omnummereret i Del 3 (dataopsamling før netværk).

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
- [ ] Certifikatkæden fra `30-certs.sh` sendes til Claude (bestemmer rod-CA på enheden i Del 6)
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

- **Mbed TLS 4.1 (PSA):** TLS-Kconfig fra ældre guides virker ikke (deprecated symboler,
  `PSA_WANT_*`-afhængigheder). Løses i del 6.
- **`CONFIG_PM` + logging:** linkerfejl `log_const_soc` i Zephyr 4.4.0. Workaround verificeret
  (registrér log-modulet `soc` i app'en). Indføres i del 9.
- **Modem uden PWRKEY:** `simcom,sim7080` kræver `mdm-power-gpios`. Pladsholder på GPIO22
  (uforbundet) + `autostarts`, og CMUX-recovery før start. Testes på hardware.
- **Ingen RTS/CTS** til modemet: risiko for tabte bytes under PPP. Holdes øje med i hardwaretesten.
- Sysbuild giver en harmløs Kconfig-warning om `MCUBOOT_UPDATE_FOOTER_SIZE` (kun relevant for OTA).
- Modbus RTU-varianten flytter console til USB-porten (USB Serial/JTAG). Den rigtige Modbus-kilde/slave er
  stadig ikke kendt. Registertabellen i `modbus.c` er et eksempel (holding 0–1, 0,1-skalering).
- Batches kasseres i `app_logic`, indtil Sparkplug er implementeret (Del 7).
- **Webdock:** passwords til admin/MySQL/FTP var synlige på et skærmbillede. Skal skiftes (05-cloud.md §3.1).
- Pc'en har ældre installationer (`C:\zephyrproject`, `C:\Users\paulm\zephyrproject`). De bruges ikke.

## Del 2 – resumé

Arkitektur (`02-arkitektur.md`), kravmatrix, modul-skelet og `app/Kconfig`. `west.yml` + `tf-psa-crypto`.
Testet på hardware 2026-11-02.

## Del 1 – resumé

Windows 11-miljø (`setup/build/flash/monitor.ps1`), Zephyr 4.4.0 + SDK 1.0.1, T2-manifest, sysbuild +
MCUboot, 4 MB flash-snippet. Testet på hardware 2026-11-02 (COM3, device-id `esp32c6-14c19fe0ab24`).
