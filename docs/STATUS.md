# STATUS – Zephyr_A (Projekt A)

Opdateres i slutningen af hver del. Detaljeret kravstatus: [02-kravmatrix.md](02-kravmatrix.md).

| Del | Emne | Branch | Status |
|---|---|---|---|
| 1 | Udviklingsmiljø Windows 11 + repo-skelet | `del-01-setup` | ✅ Godkendt |
| 2 | Design: arkitektur, tråde, IPC, pin-plan, kravmatrix, modul-skelet | `del-02-design` | Færdig – afventer test og godkendelse |
| 3 | Cellulært netværk (SIM7080G, CMUX, PPP) | – | Ikke startet |
| 4 | Cloud-server (Webdock: SSH, Mosquitto, Let's Encrypt, Node-RED) | – | Ikke startet |
| 5 | MQTT + TLS (Mbed TLS 4.1/PSA) | – | Ikke startet |
| 6 | Dataopsamling (intern temperatursensor, Modbus når kilden er kendt) | – | Ikke startet |
| 7 | Sparkplug B (nanopb) | – | Ikke startet |
| 8 | Sikkerhed: IEC 62443-4-2 / CRA | – | Ikke startet |
| 9 | Lavt strøm- og dataforbrug, LP-kerne (`remote/`) | – | Ikke startet |
| 10 | Nice to have (NCMD, dashboard, mTLS, MCUboot-signering/OTA) | – | Ikke startet |
| 11 | Afslutning: test, memory layout, rapportmateriale | – | Ikke startet |

## Del 2 – lavet

- `docs/02-arkitektur.md`: moduler og API, trådtabel, IPC, 5 Mermaid-diagrammer (dataflow,
  komponenter, sekvens måling→publish, tilstandsmaskiner for netværk og MQTT), pin- og UART-plan,
  vurdering af console på USB Serial/JTAG, strategi for strøm og data, Sparkplug-design, risici
- `docs/02-kravmatrix.md`: alle krav (G-01…G-25, A-01…A-10, N-01…N-06, O, P) med løsning, modul,
  del, status og test
- Modul-skelet i `app/src/`: `device_info`, `timesync`, `sensor`, `modbus`, `app_logic`, `sparkplug`,
  `mqtt_client`, `network`, `power` (+ `app_types.h`). Hver `.c` har `LOG_MODULE_REGISTER`, og hver `.h`
  har header guards og dokumenterede prototyper
- `app/Kconfig`: projektindstillinger (`CONFIG_APP_*`). Intervallet har `range 5000 15000`
- `main.c` initialiserer modulerne i afhængighedsrækkefølge. Device-id er flyttet til `device_info.c`
- **Rettelse til del 1:** `west.yml` mangler modulet `tf-psa-crypto`, som Mbed TLS 4.1 kræver.
  Tilføjet (ville ellers fejle i del 5)

### Verificeret

- Skelettet bygger med sysbuild + `espressif-flash-4M` uden compiler-warnings: FLASH 136 kB (3,2 %),
  RAM 51 kB (10,5 %)
- `CONFIG_APP_SAMPLE_INTERVAL_MS=20000` afvises af Kconfig ("Aborting due to Kconfig warnings")
- Alle 5 Mermaid-diagrammer renderet med Mermaid CLI
- Prototype-build med den planlagte overlay (UART1, `simcom,sim7080` + `autostarts`, coretemp) og
  modem/PPP/MQTT/SNTP/nanopb/PM: bygger (274 kB flash, 104 kB RAM uden TLS)

### Mangler (test på hardware)

- [ ] `git pull` + `setup.ps1 -SkipWinget` (henter `tf-psa-crypto`)
- [ ] `build.ps1 -Pristine`, `flash.ps1`, `monitor.ps1`: boot-log viser modulernes init-linjer og heartbeat

## Kendte problemer / noter

- **Mbed TLS 4.1 (PSA):** TLS-Kconfig fra ældre guides virker ikke (deprecated symboler,
  `PSA_WANT_*`-afhængigheder). Løses i del 5.
- **`CONFIG_PM` + logging:** linkerfejl `log_const_soc` i Zephyr 4.4.0. Workaround verificeret
  (registrér log-modulet `soc` i app'en). Indføres i del 9.
- **Modem uden PWRKEY:** `simcom,sim7080` kræver `mdm-power-gpios`. Pladsholder på GPIO22
  (uforbundet) + `autostarts`. Testes i del 3.
- **Ingen RTS/CTS** til modemet: risiko for tabte bytes under PPP. Testes i del 3.
- Sysbuild giver en harmløs Kconfig-warning om `MCUBOOT_UPDATE_FOOTER_SIZE` (kun relevant for OTA).
- Modbus-kilden er ikke afklaret. Valget (RTU/TCP) bestemmer, om console skal flyttes til USB
  Serial/JTAG (se `02-arkitektur.md` §6.3).
- Pc'en har ældre installationer (`C:\zephyrproject`, `C:\Users\paulm\zephyrproject`). De bruges ikke.

## Del 1 – resumé

Windows 11-miljø (`setup/build/flash/monitor.ps1`), Zephyr 4.4.0 + SDK 1.0.1, T2-manifest, sysbuild +
MCUboot, 4 MB flash-snippet. Testet på hardware 2026-11-02 (COM3, device-id `esp32c6-14c19fe0ab24`).
