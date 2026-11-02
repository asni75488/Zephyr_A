# STATUS – Zephyr_A (Projekt A)

Opdateres i slutningen af hver del. Detaljeret kravstatus: [02-kravmatrix.md](02-kravmatrix.md).

| Del | Emne | Branch | Status |
|---|---|---|---|
| 1 | Udviklingsmiljø Windows 11 + repo-skelet | `del-01-setup` | ✅ Godkendt |
| 2 | Design: arkitektur, tråde, IPC, pin-plan, kravmatrix, modul-skelet | `del-02-design` | ✅ Godkendt |
| 3 | Dataopsamling: intern temperatursensor, Modbus RTU, simulering | `del-03-dataopsamling` | Færdig – afventer test og godkendelse |
| 4 | Cellulært netværk (SIM7080G, CMUX, PPP) | – | Ikke startet |
| 5 | Cloud-server (Webdock: SSH, Mosquitto, Let's Encrypt, Node-RED) | – | Ikke startet |
| 6 | MQTT + TLS (Mbed TLS 4.1/PSA) | – | Ikke startet |
| 7 | Sparkplug B (nanopb) + tidssynkronisering (UTC-tidsstempler) | – | Ikke startet |
| 8 | Sikkerhed: IEC 62443-4-2 / CRA | – | Ikke startet |
| 9 | Lavt strøm- og dataforbrug, LP-kerne (`remote/`) | – | Ikke startet |
| 10 | Nice to have (NCMD, dashboard, mTLS, MCUboot-signering/OTA) | – | Ikke startet |
| 11 | Afslutning: test, memory layout, rapportmateriale | – | Ikke startet |

Delene blev omnummereret i Del 3 (dataopsamling før netværk).

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
  (uforbundet) + `autostarts`. Testes i del 4.
- **Ingen RTS/CTS** til modemet: risiko for tabte bytes under PPP. Testes i del 4.
- Sysbuild giver en harmløs Kconfig-warning om `MCUBOOT_UPDATE_FOOTER_SIZE` (kun relevant for OTA).
- Modbus RTU-varianten flytter console til USB-porten (USB Serial/JTAG). Den rigtige Modbus-kilde/slave er
  stadig ikke kendt. Registertabellen i `modbus.c` er et eksempel (holding 0–1, 0,1-skalering).
- Batches kasseres i `app_logic`, indtil Sparkplug er implementeret (Del 7).
- Pc'en har ældre installationer (`C:\zephyrproject`, `C:\Users\paulm\zephyrproject`). De bruges ikke.

## Del 2 – resumé

Arkitektur (`02-arkitektur.md`), kravmatrix, modul-skelet og `app/Kconfig`. `west.yml` + `tf-psa-crypto`.
Testet på hardware 2026-11-02.

## Del 1 – resumé

Windows 11-miljø (`setup/build/flash/monitor.ps1`), Zephyr 4.4.0 + SDK 1.0.1, T2-manifest, sysbuild +
MCUboot, 4 MB flash-snippet. Testet på hardware 2026-11-02 (COM3, device-id `esp32c6-14c19fe0ab24`).
