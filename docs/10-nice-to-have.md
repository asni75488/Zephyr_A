# Del 10 – Nice to have: task watchdog, fail2ban, mTLS og dashboard

Scope er valgt sammen med dig ud fra kravmatrixen og de huller, Del 8 og 9 efterlod. **OTA
(N-02) er udskudt**: det er det største og mest risikable punkt, og det kan ikke testes
meningsfuldt uden hardware.

| # | Nyt i Del 10 | Lukker | Hvor |
|---|---|---|---|
| 1 | **Task watchdog** for alle seks applikationstråde, med hardware-watchdog (TIMG0) som fallback. Tråden, der hang, rapporteres efter genstarten | IEC 62443-4-2 CR 7.4 / CRA I(2)(h). Del 8/9-gap "ingen task watchdog" | `src/health.c/.h` |
| 2 | **fail2ban** på serveren: MQTT (8883/8884) og SSH. Spærrer i UFW efter 10 fejl på 10 min | CR 1.11 (fejlede loginforsøg) | `cloud/fail2ban/`, `cloud/scripts/70-fail2ban.sh` |
| 3 | **mTLS (N-04):** klientcertifikat pr. enhed fra en egen device-CA. Ingen password. Spærring via CRL. Mosquitto-listener 8884 | CR 1.2/1.5/1.9 skærpet, Del 6-risiko "password i flash" | `scripts/device_pki.py`, `build.ps1 -Mtls`, `tls_ca.c`, `cloud/scripts/65-mtls.sh` |
| 4 | **Dashboard (N-03):** tabel med én række pr. edge node, knapper og sliders til kommandoer | N-03, del af N-01 | `cloud/node-red/overview.js`, `dashcmd.js`, `flows.json` |

---

## 1. Task watchdog (`health.c`)

### 1.1 Hvorfor endnu en watchdog?

LP-kernens supervisor (Del 9) ser kun målekæden (`sensor` → `app_logic`). Hænger fx
`mqtt`-tråden i et blokerende socket-kald, eller låser `network` sig i modemdriveren, bliver
der stadig målt, men intet sendt, og intet genstarter enheden. Zephyrs **task watchdog**
(`subsys/task_wdt`) giver hver tråd sin egen kanal og bruger ESP32-C6's timergruppe-watchdog
`wdt0` som hardware-fallback.

| Lag | Fanger | Tid |
|---|---|---|
| Task watchdog (kernel-timer) | En tråd, der har været *optaget* for længe (løkke, deadlock, kald der aldrig returnerer) | 180 s pr. tråd (`APP_HEALTH_TIMEOUT_S`) |
| Hardware-watchdog `wdt0` | Kernen står stille: interrupts slået fra, timer-ISR kører ikke | 10 s + 5 s (`TASK_WDT_MIN_TIMEOUT` + `HW_FALLBACK_DELAY`) |
| LP-kerne-supervisor (Del 9) | Målekæden står stille, også hvis HP-kernens ur eller strøm-domæne har problemer | 120 s |

### 1.2 Designet: kun *optaget* tid overvåges

Trådene er event-drevne og venter med god grund længe: `network` i OFF, `mqtt` mellem to
keep-alives (20 min), `uplink` 30 min mellem uplinks. En klassisk watchdog, der skal fodres
hvert N sekund, ville tvinge alle tråde til at vågne periodisk. Det koster strøm (light sleep)
og gør koden grimmere. I stedet:

```c
health_pause(HEALTH_MQTT);      // før en ventetid, der må vare "for evigt"
ret = zsock_poll(...);
health_feed(HEALTH_MQTT);       // tråden arbejder igen -> overvåges
```

`health_pause()` fjerner trådens kanal (`task_wdt_delete`), og næste `health_feed()` tilføjer
den igen. Dermed overvåges præcis den tid, en tråd *arbejder* mellem to ventetider. Det er dér,
en fejl får den til at hænge. `sensor` fodrer ved hver måling (timerstyret, maks. 15 s).
`network` gør det i sin eneste ventefunktion `take_events()`.

| Tråd | Fodres | Pauser før |
|---|---|---|
| `sensor` | hver måling | – (ventetiden er altid ≤ 15 s) |
| `app_logic` | efter hver `k_poll` | `k_poll` (venter på målinger/kommandoer) |
| `network` | efter hver `take_events` | `take_events` (OFF, backoff, registrering) |
| `mqtt` | efter poll/connect-trin | `zsock_poll` (keep-alive), vent på netværk, backoff |
| `sparkplug` | efter hver `k_poll` | `k_poll` |
| `uplink` | efter hver ventetid | ventetid til næste uplink, linger, timeout |

**180 s** dækker de længste lovlige optaget-faser: modem-suspend (op til 30 s i driveren), DNS
+ TLS-handshake + CONNACK over LTE-M og en Modbus-læsning med retries.

### 1.3 Efter en watchdog-genstart

Timeout-callbacken (ISR) skriver trådens nummer i LP-SRAM (`hp_reset_note` i `lp_shared.h`,
overlever software-reset). Den skriver også en linje med `printk` og genstarter. Efter boot:

```
<err> health: SECURITY: last reset by the task watchdog - thread 'mqtt' was stuck
```

NBIRTH viser `Properties/Reset Cause = "task watchdog (mqtt)"`, og dashboardet viser det i
kolonnen *Reset*.

---

## 2. fail2ban (CR 1.11)

Mosquitto har ingen lockout efter forkerte passwords. fail2ban læser
`/var/log/mosquitto/mosquitto.log` og spærrer kilde-IP'en i UFW.

**Filteret** (`cloud/fail2ban/filter.d/zephyr-a-mosquitto.conf`) matcher **to linjer**:
Mosquitto skriver kun IP-adressen på linjen `New connection from …`, og årsagen står på næste
linje:

| Tæller som fejl | Tæller ikke |
|---|---|
| `not authorised` (forkert bruger/password) | `unexpected eof` (en enhed mister dækning) |
| TLS afvist pga. certifikat: manglende, ukendt CA, spærret, ugyldigt | normale connects/disconnects |
| ikke-TLS-trafik på TLS-porten (`wrong version number`, `http request`) og protokolfejl (scannere) | |

| Indstilling | Værdi | Begrundelse |
|---|---|---|
| `maxretry` / `findtime` | 10 / 10 min | En enhed med forkert password prøver højst ca. 6 gange i timen (backoff 10–600 s, Del 6) og bliver aldrig spærret. En password-gætter bliver |
| `bantime` | 1 t, stigende til 1 uge ved gentagelse | |
| `ignoreip` | 127.0.0.1 + `IGNORE_IP` | **Vigtigt:** deler enhederne offentlig IP via operatørens NAT (CGNAT), ville én fejlkonfigureret enhed kunne spærre alle. Kendes Onomondos udgående adresser, angives de ved installation: `sudo IGNORE_IP="x.x.x.x/yy" ./70-fail2ban.sh` |
| `[sshd]` | 5 forsøg | SSH tager kun nøgler (Del 5). Spærringen fjerner støj og CPU-forbrug |

**Test:** filteret er kørt med det rigtige `fail2ban-regex` (fail2ban 1.1.0 fra GitHub) mod
`cloud/fail2ban/test/mosquitto-sample.log`: 7 fejl fra 5 adresser genkendt, "unexpected eof"
og vellykkede forbindelser ignoreret. Linjerne er kopieret fra Mosquitto 2.0.18's egne logs i
testsuiten. `70-fail2ban.sh` kører samme selvtest på serveren, før jail'en startes.

---

## 3. mTLS: klientcertifikat i stedet for password (N-04)

### 3.1 Hvad bliver bedre?

| | Password (Del 6) | **Klientcertifikat (Del 10)** |
|---|---|---|
| Hemmelighed på enheden | password i flash | privat nøgle (ECDSA P-256) i flash |
| Hemmelighed på serveren | password-hash (`/etc/mosquitto/passwd`) | **ingen**: serveren har kun CA-certifikat og CRL |
| Sendes hemmeligheden over linjen? | ja, i CONNECT (krypteret af TLS) | **nej**: enheden beviser kun, at den har nøglen (signatur i handshaket) |
| Udstedes af | serveradministrator (`60-add-device.sh`) | device-CA på operatørens pc (`device_pki.py`) |
| Spærring af én enhed | slet brugeren på serveren | `revoke` → ny CRL på serveren |
| Udløb | aldrig | 5 år (certifikat), CA 10 år |
| Gætbar? | ja (derfor fail2ban) | nej |

### 3.2 Hvorfor CA'en ligger på pc'en, ikke på serveren

Serveren er den mest udsatte del af systemet (offentlig IP, Node-RED, SSH). Lå CA-nøglen dér,
kunne en angriber med root på serveren udstede certifikater til vilkårlige enheds-id'er og
publicere data i deres navn. Med CA'en på pc'en (som MCUboot-nøglen fra Del 8) kan en
kompromitteret server højst *afvise* enheder, ikke *udgive sig* for dem.

### 3.3 Sådan hænger det sammen

```
PC: device_pki.py init/issue  ──ca.crt + crl.pem──▶  Server: 65-mtls.sh  ─▶ /etc/mosquitto/ca_certificates/
           │                                                                 listener 8884:
           └─<id>.crt.der + <id>.key.der ─▶ build.ps1 -Mtls ─▶ firmware        require_certificate true
                                                                               use_identity_as_username true
                                                                               crlfile ...
```

- **Mosquitto bruger certifikatets CN som brugernavn** (`use_identity_as_username`). ACL'en
  (`pattern ... %u`) virker derfor uændret: en enhed kan stadig kun bruge sine egne topics.
- **Firmware:** `tls_ca.c` registrerer certifikat + nøgle under sec tag 99 ved siden af de fire
  rod-CA'er. `mqtt_client.c` sender intet password. Før første forbindelse sammenligner den
  certifikatets CN med sit eget enheds-id. Et certifikat til en anden enhed ville forbinde fint,
  men ACL'en ville stille smide alle beskeder væk. Derfor stopper enheden med
  `SECURITY: client certificate is for 'X', this device is 'Y'`.
- **TLS 1.3-detalje:** serveren tjekker klientcertifikatet *efter* klientens del af handshaket.
  Et afvist certifikat ses derfor på enheden som en lukket forbindelse før CONNACK. Den logges
  nu som `SECURITY: broker closed the connection after the TLS handshake, before CONNACK -
  client certificate refused (revoked, expired, unknown CA)?`.
- **CRL'ens gyldighed:** OpenSSL afviser *alle* klienter, når CRL'ens `nextUpdate` er passeret.
  CRL'en udgives af os (kopieres til serveren) og hentes ikke fra en URL. Derfor er den gyldig i
  10 år, og `90-verify.sh` advarer, hvis der er under 30 dage tilbage.
- **Overgang:** listener 8883 (password) bliver ved med at køre, indtil alle enheder er flyttet.
  Derefter lukkes den udefra med `sudo ./65-mtls.sh --close-8883`.

### 3.4 Pris

| | Uden mTLS | Med mTLS |
|---|---|---|
| Flash | 438 892 B | 440 116 B (+1,2 kB) |
| RAM | 41,52 % | 41,52 % |
| Handshake (målt, samme 3 s-session) | 1303 B op / 3963 B ned | 1873 B op / 4893 B ned (**+0,57 / +0,93 kB**) |
| Pr. døgn | continuous: ét handshake pr. forbindelse, ubetydeligt. `-LowPower`: 48 × 1,5 kB ≈ **+72 kB** (ca. +12 %) | |

### 3.5 Kommandoer

```powershell
# PC (én gang pr. enhed). Enheds-id står i boot-loggen: "Device id: esp32c6-..."
.\scripts\new-device-cert.ps1 -DeviceId esp32c6-14c19fe0ab24
scp certs\device-pki\ca.crt certs\device-pki\crl.pem admin@zephyra.vps.webdock.cloud:~/
```
```bash
# Server
cd ~/Zephyr_A && git pull && git checkout del-10-nice
sudo bash cloud/scripts/65-mtls.sh ~/ca.crt ~/crl.pem
```
```powershell
# PC: byg og flash med certifikat
.\scripts\build.ps1 -Mtls -DeviceId esp32c6-14c19fe0ab24 -Pristine
.\scripts\flash.ps1 -Port COM3
# Spær en enhed:
.\scripts\new-device-cert.ps1 -DeviceId esp32c6-14c19fe0ab24 -Revoke
#   kopier crl.pem til serveren, derefter: sudo bash cloud/scripts/65-mtls.sh --crl ~/crl.pem
```

### 3.6 Rest-risiko og næste skridt

Den private nøgle ligger stadig i klartekst i flash, ligesom passwordet gjorde. Med fysisk
adgang kan den læses ud. Forskellen er, at en udlæst nøgle kan **spærres** uden at røre de
andre enheder, og at serveren ikke længere gemmer noget, der kan misbruges. Den fulde løsning:

1. **Flash encryption** (Del 8 §7, irreversibel eFuse).
2. ESP32-C6's **Digital Signature-periferi (DS)**: nøglen krypteres med en HMAC-nøgle i en
   eFuse, som software aldrig kan læse. Signaturen i TLS-handshaket laves i hardware. Kræver en
   Mbed TLS-hook (`MBEDTLS_PK_..._ALT`/PSA-driver) og eFuse-programmering, altså et
   produktions-skridt.
3. Nøglen genereres *på enheden*, og kun en CSR sendes ud. Så har nøglen aldrig været på pc'en.

---

## 4. Dashboard (N-03)

Dashboard 2.0 (`https://zephyra.vps.webdock.cloud:1880/dashboard`, login som i Del 5) har nu
tre grupper:

| Gruppe | Indhold | Kilde |
|---|---|---|
| Målinger | graf + seneste besked (Del 5/7) | `decode.js` |
| **Edge nodes** | én række pr. node: status (online/offline og hvor længe), sidste data, firmware, reset-årsag, sikkerhedsstatus (eFuses), RSSI, MQTT-forbindelser, interval, batch, **kommandoer i kø** | `overview.js` (fodres med hver dekodet besked + hvert 30. s) |
| **Kommandoer** | vælg node (eller alle), knapper *Rebirth*, *Reboot*, *Deep sleep 10 min*, sliders *Måleinterval 5–15 s* og *Batch 1–12* | `dashcmd.js` → samme NCMD-encoder som inject-knapperne |

**Hvorfor sådan:**

- Dashboard-kommandoer går gennem **samme** `ncmd.js` som inject-knapperne. De får derfor
  automatisk køen til offline noder fra Del 9, og der er kun ét sted, NCMD bygges.
- `dashcmd.js` tjekker **de samme grænser som firmwaren** (5–15 s, 1–12, 60–86400 s). En forkert
  værdi giver en synlig fejl i Node-RED i stedet for en kommando, som enheden stille afviser.
- Sliders sender kun ved slip (`outs: end`). Ellers ville hver mellemværdi blive en NCMD og
  koste data.
- Dashboardet er beskyttet af `httpNodeAuth` (Del 5). Alle, der kan logge ind, kan sende
  kommandoer. Der er ingen roller (se §6).

Nodedefinitionerne er skrevet efter Dashboard 2.0 v1.36's kildekode (`nodes/widgets/ui_*.html`
på GitHub). `test/flows.test.js` sikrer, at function-noderne i `flows.json` er identiske med
de testede `.js`-filer, at alle wires og grupper findes, og at alle dashboard-kontroller ender
i NCMD-encoderen.

**Import:** Menu → Import → `cloud/node-red/flows.json` → *Replace* (samme node-id'er som før,
så eksisterende noder opdateres).

---

## 5. Test

| Test | Resultat |
|---|---|
| `app/tests/health` (ztest, simuleret tid) | **5/5**: grund til sidste reset rapporteres, fodret tråd udløber aldrig (200 s), optaget tråd udløber efter 30 s (ikke 29), tråd i pause må vente 600 s og overvåges igen bagefter, alle 6 tråde får en kanal |
| `app/tests/mqtt_tls/run.sh` (native_sim + Mosquitto 2.0.18) | **105/105** (91 + 14 nye i scenarie 14). mTLS: forbindelse med certifikat, brokeren bruger CN som brugernavn, ACL via CN, data leveres. Spærret certifikat afvises (CRL), og enheden logger `SECURITY:`. Certifikat til en anden enhed opdages før forbindelse. Enhed uden certifikat afvises af en listener, der kræver certifikat. Ingen falsk watchdog-alarm i nogen af de øvrige scenarier |
| `app/tests/lp_supervisor` | 7/7 (uændret, `hp_reset_note` tilføjet uden at ændre layoutet) |
| `scripts/device_pki.py` | init/issue/revoke/crl/list. `openssl verify` godkender kæden og afviser det spærrede certifikat (`-crl_check`). Ugyldigt id afvises |
| fail2ban-filter | `fail2ban-regex` 1.1.0: 7 fejl / 5 adresser, ingen falske |
| `cloud/node-red/test/*.test.js` | `overview`, `dashcmd`, `ncmd`, `decode`, `flows`: bestået |
| ESP32-C6-builds | standard: FLASH 10,46 %, RAM 41,52 %. mTLS: +1,2 kB. Ingen nye warnings |
| Scripts | ShellCheck: `65-mtls.sh`, `70-fail2ban.sh`, `90-verify.sh`, `run.sh`. PowerShell-parser: `build.ps1`, `new-device-cert.ps1` |

### 5.1 Hardwaretest (når boardet er tilgængeligt)

| # | Test | Forventet |
|---|---|---|
| 1 | Boot med standard-build | `Task watchdog: 180 s per thread, hardware fallback wdt0`. Ingen reset i 1 time |
| 2 | **Hæng-test:** tilføj midlertidigt `k_sleep(K_FOREVER);` i `mqtt`-trådens `session_run()` lige efter `drain_tx_queue()` (ikke committet). Brug ikke en tom `for (;;)`: `mqtt` har højere prioritet end `app_logic`, så LP-supervisoren (120 s) ville nå først | Målingerne fortsætter, men intet sendes. Efter ca. 180 s: `SECURITY: task watchdog - thread 'mqtt' stuck`, reboot, NBIRTH `Reset Cause = task watchdog (mqtt)` |
| 3 | **HW-fallback:** midlertidigt `irq_lock(); for (;;) { }` i `app_logic` | Reset efter ca. 15 s (wdt0), uden besked fra task watchdog |
| 4 | mTLS (§3.5) | `mTLS: client certificate CN=esp32c6-..., port 8884`, data i Node-RED. Mosquitto-log: `u'esp32c6-...'` |
| 5 | Spær enheden (§3.5) | Enheden logger `SECURITY: broker closed the connection ... refused`, backoff |
| 6 | fail2ban: 11 forkerte logins fra pc'en: `for ($i=0;$i -lt 11;$i++){ mosquitto_pub -h zephyra.vps.webdock.cloud -p 8883 --capath ... -u x -P y -t t -m m }` (eller fra en anden maskine) | `sudo fail2ban-client status zephyr-a-mosquitto` viser pc'ens IP. Fjern med `unbanip` |
| 7 | Dashboard | Tabellen viser noden. *Måleinterval 5 s* → NDATA, kolonnen *Interval [s]* = 5 |

---

## 6. Kendte begrænsninger

| Begrænsning | Konsekvens | Videre |
|---|---|---|
| OTA (N-02) ikke lavet | Firmware opdateres kun over USB | MCUboot swap + download (MQTT/HTTPS) med samme signaturnøgle (Del 8) |
| Privat nøgle i klartekst i flash | Fysisk adgang → nøglen kan læses (kan spærres) | §3.6: flash encryption / DS-periferi |
| Dashboard: én fælles login, ingen bekræftelse ved Reboot/Sleep | Kan ikke se, hvem der sendte hvad. Fejlklik udføres | Brugere + roller i `httpNodeAuth`/FlowFuse, `ui-notification` som bekræftelse |
| fail2ban og CGNAT | En fejlkonfigureret enhed kunne i værste fald spærre andre enheder bag samme IP | `IGNORE_IP` med operatørens udgående net. Med mTLS + lukket 8883 er password-gætning ikke længere mulig |
| Watchdog-reset-årsag kun med LP-kerne | På andre targets (native_sim) går noten tabt ved reset | – |
| TLS-sessionsgenoptagelse (Del 9 §5.1) | Fuldt handshake pr. uplink, nu +1,5 kB med mTLS | Egen MQTT-transport |

## 7. Definition of done

- [ ] Standard-build kører 1 time uden watchdog-reset. Hæng-test (§5.1 nr. 2) giver `task watchdog (mqtt)`.
- [ ] `70-fail2ban.sh` kører uden fejl (selvtest OK). 11 forkerte logins spærrer pc'ens IP.
- [ ] `new-device-cert.ps1` + `65-mtls.sh` + `build.ps1 -Mtls`: enheden forbinder på 8884 uden password. `git status` viser ingen filer fra `certs\device-pki`. Backup af `ca.key` er taget.
- [ ] Spærring: enheden afvises efter `--crl`. Med et nyt certifikat virker den igen.
- [ ] Dashboardet viser tabellen. Kommandoer fra knapper og sliders virker (også i kø til en offline node).
- [ ] `90-verify.sh`: alle checks OK (8884, CRL, fail2ban).
