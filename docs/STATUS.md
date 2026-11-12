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
| 7 | Sparkplug B + tidssynkronisering | `del-07-sparkplug` | Færdig – testet på native_sim, **ikke på hardware endnu** (bruger gik videre til Del 8) |
| 8 | Sikkerhed: IEC 62443-4-2 / CRA, signeret firmware | `del-08-sikkerhed` | Færdig – testet på native_sim og med `imgtool`, **ikke på hardware endnu** (bruger gik videre til Del 9) |
| 9 | Lavt strøm- og dataforbrug, LP-kerne (`remote/`) | `del-09-strom` | Færdig – testet på native_sim (91/91 + LP-unittest 7/7), **ikke på hardware/strømmålt endnu** (bruger gik videre til Del 10) |
| 10 | Nice to have: task watchdog, fail2ban, mTLS, dashboard (OTA udskudt) | `del-10-nice` | Færdig – testet på native_sim (105/105 + health 5/5), **ikke på VPS/hardware endnu** (bruger gik videre til Del 11) |
| 11 | Afslutning: test, memory layout, rapportmateriale | `del-11-afslutning` | Færdig – alle host-tests grønne lokalt og i CI, afventer hardware-acceptprotokol (`11-afslutning.md` §4) og godkendelse |

Delene blev omnummereret i Del 3 (dataopsamling før netværk).

## Del 11 – lavet

- **Memory layout** (N-06): `scripts/memory_layout.py` (flash, HP-SRAM, LP-SRAM fra build-outputtet, `--check` med grænser),
  `app/overlay-stack-usage.conf` (thread analyzer til stack-måling på hardware)
- **Samlet test:** `scripts/run-host-tests.sh` (ztest ×2, native_sim + Mosquitto, Node-RED, PKI, fail2ban, ShellCheck, memory),
  **CI** i `.github/workflows/host-tests.yml` (jobs `tools` og `firmware`, samme image som devcontaineren)
- **Hardware-acceptprotokol** (20 trin, Del 3–10 i rækkefølge) og **rapportmateriale** (arkitektur, designvalg,
  nøgletal, fund E1–E10, begrænsninger) i `docs/11-afslutning.md`. `CHANGELOG.md` pr. del
- `.devcontainer/post-create.sh` ShellCheck-ren (`west packages pip --install`). VERSION 0.11.0-del11
- Standard forbliver continuous: strømmålingen, som skulle afgøre skiftet, mangler

### Verificeret

- `run-host-tests.sh` lokalt: 8/8 trin PASS (lp_supervisor 7/7, health 5/5, mqtt_tls 105/105, Node-RED 5 filer, PKI,
  fail2ban, ShellCheck 15 scripts, memory-grænser)
- **CI på GitHub grøn** (run 43224238403): `tools` (Node-RED, ShellCheck, PowerShell, PKI, fail2ban) og `firmware`
  (lp_supervisor 7/7, health 5/5, **mqtt_tls 105/105** mod Mosquitto 2.0.18 fra Ubuntu i Zephyrs build-image)
- Undervejs fundet og rettet: SDK-sti for root i CI-containeren, IPv4-tjek af hosts-opslaget (E11, E12)
- `memory_layout.py --check` på standard-buildet: slot0 24 %, SRAM 42 %, LP 38 %
- Stack-overlay bygger (SRAM 42,3 %)

### Mangler (hardware)

- [ ] Acceptprotokollen `11-afslutning.md` §4 (samler alle åbne hardwarepunkter fra Del 3–10)
- [ ] Stack-tabellen §2.2

## Del 10 – lavet

Scope valgt sammen med brugeren: task watchdog + fail2ban, mTLS (N-04), dashboard (N-03). **OTA (N-02) er udskudt.**

- **Task watchdog** (`health.c`): kanal pr. tråd (6), overvåger kun den tid, en tråd arbejder (pause under lovlige
  ventetider), 180 s. Hardware-fallback `wdt0` (15 s). Tråden, der hang, gemmes i LP-SRAM og vises i NBIRTH
  (`Reset Cause = task watchdog (<tråd>)`)
- **fail2ban** (`cloud/fail2ban/`, `70-fail2ban.sh`): to-linjers filter til Mosquitto 2.0 (IP + årsag), 10 fejl / 10 min →
  spærring i UFW (1 t, stigende). `unexpected eof` (tabt dækning) tæller ikke. `IGNORE_IP` til operatørens NAT
- **mTLS:** `scripts/device_pki.py` (device-CA på pc'en, certifikat pr. enhed, CRL), `new-device-cert.ps1`,
  `build.ps1 -Mtls`, Mosquitto-listener 8884 (`65-mtls.sh`, `--crl`, `--close-8883`). Firmware: ingen password,
  CN-tjek mod enheds-id, `SECURITY:`-log når brokeren afviser certifikatet. `90-verify.sh` tjekker 8884, CRL og fail2ban
- **Dashboard:** tabel pr. edge node + knapper/sliders (Rebirth, Reboot, Deep sleep, interval, batch) gennem samme
  NCMD-encoder (kø til offline noder). Nye function-filer `overview.js`, `dashcmd.js` med unittests og `flows.test.js`
- `docs/10-nice-to-have.md`. VERSION 0.10.0-del10

### Verificeret

- native_sim mod Mosquitto 2.0.18: **105/105** (14 nye i scenarie 14, mTLS). Ingen falsk watchdog-alarm
- `app/tests/health` (ztest): **5/5**. `app/tests/lp_supervisor`: 7/7
- mTLS-handshake målt: +0,57 kB op / +0,93 kB ned
- fail2ban-filter med `fail2ban-regex` 1.1.0: 7 fejl / 5 adresser, ingen falske
- `device_pki.py` krydstjekket med `openssl verify` (kæde OK, spærret certifikat afvist)
- ESP32-C6: standard FLASH 10,46 % / RAM 41,52 %, mTLS +1,2 kB, ingen nye warnings
- Node-RED-tests (`overview`, `dashcmd`, `ncmd`, `decode`, `flows`), ShellCheck, PowerShell-parser

### Mangler (VPS/hardware)

- [ ] Del 10-tjeklisten i `10-nice-to-have.md` §7 (hæng-test, fail2ban på VPS'en, mTLS på VPS + board, dashboard-import)
- [ ] Del 3–9 på hardware/VPS

## Del 9 – lavet

- **LP-kernen** (`app/remote/`, sysbuild-image `zephyr_a_lpcore`, 1577 B): kører hvert sekund, vækker HP-kernen fra
  deep sleep og nulstiller den, hvis målekæden står stille i 120 s (supervisor). Delt hukommelse og logik i
  `app/common/lp_shared.h`. Det signerede HP-billede tjekker LP-billedets SHA-256 ved boot
- **Light sleep** (`CONFIG_PM`, RTC-timer som vækkekilde), låst mens modem-UART eller en måling er aktiv
- **Interval-uplink** (`uplink.c`, `build.ps1 -LowPower`): modem kun tændt under en uplink hvert 30. min (eller ved ¾
  fuld kø), NDEATH + ren MQTT DISCONNECT, data med `is_historical`, batch 12. Continuous er stadig standard
- Modem: `AT+CFUN=0` når linket stoppes, `AT+CPSMS=0` (ingen PWRKEY), eDRX valgfri (`APP_MODEM_EDRX`)
- NCMD `Node Control/Sleep` (alias 11): deep sleep 60 s–24 t, LP-kernen vækker. Reset Cause `lp-core supervisor`
- Node-RED: kommandoer til offline noder sættes i kø og sendes ved næste NBIRTH, ny knap "Deep sleep 10 min"
- Workaround F3 (`soc_log.c`). `docs/09-strom-data.md`. VERSION 0.9.0-del09

### Verificeret

- Builds uden nye warnings: standard (3 images, FLASH 10,45 %, RAM 41,4 %), `-LowPower` (RAM 44,6 %)
- `app/tests/lp_supervisor` (ztest): **7/7**
- native_sim mod Mosquitto 2.0.18: **91/91** (inkl. interval-forløbet: én NDEATH pr. session med samme bdSeq som
  NBIRTH, kun rene disconnects hos brokeren, tidlig uplink, historiske data i tidsorden, NCMD Sleep afvist uden LP)
- Målt med byte-tællende proxy: fast pris pr. uplink ca. 0,8 kB op + 3,9 kB ned (TLS)
- `ncmd.test.js` (kø) og `decode.test.js` bestået. `build.ps1` parser fejlfrit

### Mangler (hardware)

- [ ] Del 9-tjeklisten i `09-strom-data.md` §9, især strømmålingen (§6.3) og Onomondo-forbruget
- [ ] Del 3–8 på hardware/VPS

## Del 8 – lavet

- **Signeret firmware (N-05):** MCUboot med ECDSA P-256 (`app/sysbuild.conf`), `BOOT_VALIDATE_SLOT0=y` (signaturen
  tjekkes ved hver boot) og downgrade-spærring (`app/sysbuild/mcuboot.conf`). Egen nøgle i `certs\` (gitignored) via
  `scripts/new-signing-key.ps1`. `build.ps1` bruger den automatisk (ellers MCUboots dev-nøgle + advarsel) og kører
  `imgtool verify`. `flash.ps1 -AppOnly` til den negative test
- `security.c`: SAR ADC-entropi til RNG'en, sikkerhedsstatus fra eFuses (secure boot, flash encryption, JTAG, download
  mode) i boot-log og NBIRTH (`Properties/Security`, alias 10)
- `SECURITY:`-præfiks på sikkerhedshændelser (TLS/CONNACK-fejl, afviste kommandoer, urimelig tid, reboot)
- NCMD Reboot afvises de første 60 s efter boot (reboot-loop), stack canaries
- `overlay-release.conf` + `build.ps1 -Release` (ingen shell/TLS-debug), `build.ps1 -Sbom` (SPDX via `west spdx`)
- `docs/08-sikkerhed.md`: zoner/conduits, STRIDE, SL-vurdering, IEC 62443-4-2- og CRA bilag I-mapping, eFuse-hærdning
  (kun beskrevet, irreversibel). `SECURITY.md` (rapportering af sårbarheder)
- VERSION 0.8.0-del08

### Verificeret

- Signeret build: `imgtool verify` OK. Samme image mod dev-nøglen → "No signature found". Én ændret byte → "invalid hash"
- MCUboot-konfigurationen indeholder den offentlige nøgle, slot0-validering og downgrade-spærring
- Builds uden warnings: standard (FLASH 10,3 %, RAM 39,6 %), `-Release`, `-TlsDebug`, `-ModbusRtu -Simulation`
- native_sim mod Mosquitto 2.0.18: **75/75** (inkl. reboot-spærring, `SECURITY:`-hændelser, ingen password eller
  privat nøgle i logs, `Properties/Security` i NBIRTH)
- SBOM genereret med Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0 og nanopb 1.0.0 (CPE/purl)
- eFuse-feltnavne kontrolleret mod espefuse 5.2 (virtuel ESP32-C6)

### Mangler (hardware)

- [ ] Del 8-tjeklisten i `08-sikkerhed.md` §9, især den negative signaturtest på boardet
- [ ] Del 3–7 på hardware/VPS

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
