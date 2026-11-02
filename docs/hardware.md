# Hardware og pin-tabel

Alle afvigelser fra boardets devicetree
(`zephyr/boards/espressif/esp32c6_devkitc/esp32c6_devkitc_hpcore.dts` i Zephyr 4.4.0)
laves i `app/app.overlay` og dokumenteres her.

## Komponenter

| Komponent | Model | Bemærkning |
|---|---|---|
| MCU-board | Espressif ESP32-C6-DevKitC-1, chip rev. v0.2, **4 MB flash** | Zephyr board target: `esp32c6_devkitc/esp32c6/hpcore` (HP-kerne), `esp32c6_devkitc/esp32c6/lpcore` (LP-kerne) |
| Cellulært modul | M5Stack S003 "M5Stamp CAT-M" (SIMCom SIM7080G) | LTE Cat-M / NB-IoT, AT-kommandoer over UART 115200 8N1 |
| SIM | Onomondo | Ingen PIN |
| Antenne | Medfølgende SMA-antenne via IPEX-kabel | Skal altid være monteret, når modemet er tændt |

## Afvigelse: flash-størrelse (4 MB i stedet for 8 MB)

Boardets devicetree antager et modul med 8 MB flash (`esp32c6_wroom_n8.dtsi`). Projektets
board har **4 MB**. Det viste første test i del 1, hvor bootloaderen skrev:

```
W (spi_flash): Detected size(4096k) smaller than the size in the binary image header(8192k). Using detected size.
```

**Løsning:** Espressifs officielle board-variant-snippet `espressif-flash-4M`
(`zephyr/snippets/espressif`, dokumenteret i `boards/espressif/common/board-variants.rst`).
Den sætter `flash0` til 4 MB og vælger partitionstabellen til 4 MB. `build.ps1` tilføjer
`-S espressif-flash-4M`, og med sysbuild gælder snippet'en for både applikationen og MCUboot
(verificeret: `CONFIG_FLASH_SIZE=4194304` og `--esp-flash-size=4MB` i begge images).

Hvorfor det er vigtigt: Med forkert størrelse ville partitioner (MCUboot-slots, storage)
kunne placeres over 4 MB. Det giver fejl, så snart OTA eller persistent lagring tages i
brug.

## Pin-tabel

| ESP32-C6 GPIO | Funktion i Zephyr | Forbundet til | Del | Status |
|---|---|---|---|---|
| GPIO16 | UART0 TX (console/log) | USB-UART-bro ("UART"-porten) | 1 | Board-standard, uændret |
| GPIO17 | UART0 RX (console/log) | USB-UART-bro ("UART"-porten) | 1 | Board-standard, uændret |
| GPIO12 / GPIO13 | USB D- / D+ (USB Serial/JTAG) | "USB"-porten | – | Board-standard, uændret |
| GPIO18 | UART1 TX | Modulets RX | 3 | Planlagt afvigelse (overlay i del 3). Pinctrl `UART1_TX_GPIO18` verificeret |
| GPIO19 | UART1 RX | Modulets TX | 3 | Planlagt afvigelse (overlay i del 3). Pinctrl `UART1_RX_GPIO19` verificeret |
| GPIO22 | `mdm-power-gpios` (pladsholder) | **Ikke forbundet** | 3 | Kræves af `simcom,sim7080`-bindingen, men modulet har ingen PWRKEY (se `02-arkitektur.md` §6.4) |
| – | `coretemp` (intern temperatursensor) | – | 6 | `status = "okay"` i overlay (disabled i SoC-dtsi) |
| GPIO20/21/23 | Reserveret til evt. RS485 (Modbus RTU) | – | senere | Kun hvis Modbus RTU vælges. Så flyttes console til USB Serial/JTAG |
| 5V | Forsyning | Modulets 5V | – | Se strøm-note nedenfor |
| GND | Stel | Modulets GND | – | |

GPIO18/19 er almindelige I/O-pins på header J3 (alternative funktioner SDIO/FSPICS),
ikke strapping-pins, så de er frie til UART1. Det verificeres endeligt i del 3 mod
pinctrl-definitionerne i Zephyr 4.4.0 (`esp32c6-gpio-sigmap.h`). Makroerne findes (tjekket i del 2).

UART-planen og vurderingen af console på USB Serial/JTAG står i `02-arkitektur.md` §6.

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
