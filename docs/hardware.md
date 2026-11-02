# Hardware og pin-tabel

Alle afvigelser fra boardets devicetree
(`zephyr/boards/espressif/esp32c6_devkitc/esp32c6_devkitc_hpcore.dts` i Zephyr 4.4.0)
laves i `app/app.overlay` og dokumenteres her.

## Komponenter

| Komponent | Model | Bemærkning |
|---|---|---|
| MCU-board | Espressif ESP32-C6-DevKitC-1 | Zephyr board target: `esp32c6_devkitc/esp32c6/hpcore` (HP-kerne), `esp32c6_devkitc/esp32c6/lpcore` (LP-kerne) |
| Cellulært modul | M5Stack S003 "M5Stamp CAT-M" (SIMCom SIM7080G) | LTE Cat-M / NB-IoT, AT-kommandoer over UART 115200 8N1 |
| SIM | Onomondo | Ingen PIN |
| Antenne | Medfølgende SMA-antenne via IPEX-kabel | Skal altid være monteret, når modemet er tændt |

## Pin-tabel

| ESP32-C6 GPIO | Funktion i Zephyr | Forbundet til | Del | Status |
|---|---|---|---|---|
| GPIO16 | UART0 TX (console/log) | USB-UART-bro ("UART"-porten) | 1 | Board-standard, uændret |
| GPIO17 | UART0 RX (console/log) | USB-UART-bro ("UART"-porten) | 1 | Board-standard, uændret |
| GPIO12 / GPIO13 | USB D- / D+ (USB Serial/JTAG) | "USB"-porten | – | Board-standard, uændret |
| GPIO18 | UART1 TX | Modulets RX | 3 | Planlagt afvigelse (overlay i del 3) |
| GPIO19 | UART1 RX | Modulets TX | 3 | Planlagt afvigelse (overlay i del 3) |
| 5V | Forsyning | Modulets 5V | – | Se strøm-note nedenfor |
| GND | Stel | Modulets GND | – | |

GPIO18/19 er almindelige I/O-pins på header J3 (alternative funktioner SDIO/FSPICS),
ikke strapping-pins, så de er frie til UART1. Det verificeres endeligt i del 3 mod
pinctrl-definitionerne i Zephyr 4.4.0 (`esp32c6-gpio-sigmap.h`).

## Kendte begrænsninger

- **Ingen PWRKEY/RESET-stift på modulet.** SIM7080G tænder, når det får 5 V, og kan kun
  styres via AT-kommandoer (`AT+CFUN`, `AT+CPOWD`, PSM/eDRX). En hård reset eller
  total slukning kræver en transistor/load-switch i 5 V-forsyningen (mulig forbedring).
- **Strøm:** Modulet forsynes fra DevKitC'ets 5 V (USB). Cellulære moduler trækker
  korte strømspidser. Firmwaren logger reset-årsagen ved opstart; `brownout` er tegn
  på, at forsyningen ikke kan følge med.
- **3,3 V-stiften på modulet** må ikke forbindes samtidig med 5 V.
- **"Rf"** på modulet: endnu ikke afklaret om det er antennestikket eller en stift
  (fx RI, ring indicator). Afklares i del 3.
