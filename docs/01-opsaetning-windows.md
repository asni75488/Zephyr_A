# Del 1 – Udviklingsmiljø på Windows 11 og repo-skelet

Denne del sætter et reproducerbart Zephyr **4.4.0**-miljø op på Windows 11 og
bygger en første firmware, der logger `boot`, device-id og Zephyr-version hvert
5. sekund.

Alle versioner og kommandoer er tjekket mod Zephyr v4.4.0-kildekoden
(`doc/develop/getting_started/index.rst`, `boards/espressif/...`,
`scripts/west_commands/...`) – ikke mod "latest"-dokumentationen.

---

## 1. Designvalg (til rapporten)

### 1.1 West-topologi: T2 (repoet er manifestet)

| | T2: app-repo er manifest (valgt) | Freestanding app |
|---|---|---|
| Zephyr-version | Pinnet i `west.yml` (`v4.4.0`) i selve repoet | Afhænger af hvad der er installeret på pc'en |
| Moduler | Kun dem projektet bruger (`name-allowlist`) | Alle ~60 moduler fra en fuld `west init` |
| Reproducerbarhed | `git clone` + `west update` giver præcis samme kode hos alle (censor, Micro Technic) | Skal dokumenteres manuelt |
| Opsætning | `west init -l` | Kræver en eksisterende workspace |

**Begrundelse:** Projektet stiller krav om en bestemt Zephyr-version (4.4.0). Med T2
er versionen en del af koden og bliver versioneret i git sammen med firmwaren.
Det er samme princip som en lockfile – man kan altid genskabe præcis det miljø,
firmwaren blev bygget med. Det er også en del af sikkerhedsargumentet senere
(CRA: kendt software-sammensætning).

### 1.2 Moduler i `west.yml`

| Modul | Hvorfor |
|---|---|
| `hal_espressif` | Hardware-abstraktion til ESP32-C6, RF-blobs, LP-core-understøttelse og `west espressif monitor` |
| `mbedtls` | TLS-stakken til den sikre MQTT-forbindelse (Mbed TLS 4.1) |
| `tf-psa-crypto` | PSA Crypto-kernen, som Mbed TLS 4.x bygger på (tilføjet i del 2, se `02-arkitektur.md` §9) |
| `mcuboot` | Bootloaderen, som `esp32c6_devkitc` bruger som standard med sysbuild. Giver også mulighed for signeret boot/OTA senere |
| `nanopb` | Protocol Buffers-encoder – Sparkplug B-payloads er protobuf |

Bevidst **ikke** med: `cmsis`/`cmsis_6` (kun til ARM Cortex-M – ESP32-C6 er RISC-V),
`picolibc` (C-biblioteket følger med SDK'ets toolchain), alle andre HAL'er.
Revisionerne for modulerne tages automatisk fra `zephyr/west.yml` ved tag v4.4.0, så de
altid passer sammen med Zephyr-versionen.

### 1.3 Zephyr SDK 1.0.1

Den præcise SDK-version står i Zephyr-kildekoden: filen `zephyr/SDK_VERSION` ved tag
v4.4.0 indeholder `1.0.1`. Det er den version, Zephyr-projektet selv tester 4.4.0 med,
så den gætter vi ikke på. `setup.ps1` læser filen og bruger versionen derfra.

Kun **RISC-V-toolchain'en** (`riscv64-zephyr-elf`) installeres, fordi både
ESP32-C6's HP-kerne og LP-kerne er RISC-V. Det sparer flere GB i forhold til at
installere alle toolchains.

### 1.4 Sysbuild + MCUboot

Micro Technics mappestruktur indeholder `Kconfig.sysbuild` og `sysbuild.cmake`, fordi
projektet skal bygge flere images: HP-kerne-applikationen og LP-kerne-programmet i
`remote/`. Det er præcis samme opbygning som Zephyrs egne LP-core-eksempler
(`samples/boards/espressif/ulp/lp_core/*`).

Med sysbuild vælger boardet `esp32c6_devkitc` som standard **MCUboot** som bootloader
(`boards/espressif/esp32c6_devkitc/Kconfig.sysbuild`). Vi beholder det. Så er
bootloaderen på plads til senere signaturverifikation og OTA (nice-to-have).
Signering er indtil videre slået fra (`BOOT_SIGNATURE_TYPE_NONE`, boardets standard).

### 1.5 Device-id fra hwinfo

Device-id'et bygges med Zephyrs `hwinfo_get_device_id()`. På ESP32-C6 returnerer
driveren (`drivers/hwinfo/hwinfo_esp32.c`) chippens fabriks-MAC-adresse fra eFuse
(6 bytes). Id'et bliver fx `esp32c6-404ccaf01234` (12 hex-tegn). Det er unikt pr. chip og
overlever genflashing, og det skal senere bruges som Sparkplug B edge node-id og i
hver MQTT-besked.

### 1.6 Logging

`CONFIG_LOG_MODE_DEFERRED=y`: log-kald lægger kun beskeden i en kø, og en tråd med lav
prioritet skriver den ud. Så bliver tidskritiske tråde (modem, MQTT) ikke forsinket af
UART-udskrift. Hver `.c`-fil får sin egen `LOG_MODULE_REGISTER`, så log-niveauet kan
styres pr. modul.

Reset-årsagen logges ved opstart. Er den `brownout`, kommer den som en **warning**,
fordi det typisk betyder, at modemet trækker for meget strøm fra USB-forsyningen.

---

## 2. Forudsætninger

- Windows 11 med **winget** (følger med Windows 11; tjek med `winget --version`)
- Ca. 3 GB fri diskplads (workspace + SDK med kun RISC-V)
- ESP32-C6-DevKitC-1 og et USB-C **datakabel** (nogle kabler kan kun lade)

### Dine eksisterende installationer

Der ligger allerede to Zephyr-installationer og en SDK på pc'en:

| Sti | Indhold | Brug |
|---|---|---|
| `C:\zephyrproject` | Zephyr 4.4.0 (gammel freestanding workspace) | Bruges ikke længere. Kan slettes, når C:\zp virker |
| `C:\Users\paulm\zephyrproject` | Zephyr 4.5.99 (udviklingsversion/main) | **Må ikke bruges** – forkert version |
| `C:\Users\paulm\zephyr-sdk-1.0.1` | Zephyr SDK 1.0.1 med RISC-V-toolchain | **Genbruges** af `setup.ps1` (ingen ny download) |

`setup.ps1` sætter `west config zephyr.base-prefer configfile`. Så bruger west altid
Zephyr fra `C:\zp`, også selvom miljøvariablen `ZEPHYR_BASE` peger på en af de gamle
installationer.

---

## 3. Hurtig vej: `setup.ps1`

Åbn **PowerShell som almindelig bruger** (ikke administrator):

```powershell
# 1) Tillad lokale scripts (engang pr. bruger) – kræves også for venv-aktivering
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser

# 2) Git skal være installeret for at kunne klone (spring over hvis 'git --version' virker)
winget install --exact --id Git.Git

# 3) Klon repoet til den korte workspace-sti
New-Item -ItemType Directory -Force C:\zp | Out-Null
git -c core.longpaths=true clone https://github.com/asni75488/Zephyr_A.git C:\zp\zephyr_a
cd C:\zp\zephyr_a

# 4) Kør opsætningen (kan køres igen uden problemer – den springer færdige trin over)
.\scripts\setup.ps1
```

Hvis `setup.ps1` installerer nye værktøjer med winget og derefter fejler med
"not found on PATH", så **luk terminalen, åbn en ny** og kør scriptet igen.

Parametre:

| Parameter | Effekt |
|---|---|
| `-SkipWinget` | Spring værktøjstjekket over (når alt er installeret) |
| `-FullClone` | Fuld git-historik i `west update` (standard er shallow `--depth=1`, sparer tid og plads) |

---

## 4. Manuelle trin (det samme som scriptet gør)

### 4.1 Værktøjer (winget)

Pakkelisten er taget direkte fra Zephyr 4.4.0's Getting Started-guide:

```powershell
winget install Kitware.CMake Ninja-build.Ninja oss-winget.gperf Python.Python.3.12 Git.Git oss-winget.dtc wget 7zip.7zip
```

7-Zip lægger sig ikke selv i PATH, men `west sdk install` skal bruge `7z.exe` til at
pakke SDK'en ud (Windows-arkiverne er `.7z`):

```powershell
$u = [Environment]::GetEnvironmentVariable('Path','User')
[Environment]::SetEnvironmentVariable('Path', "$u;C:\Program Files\7-Zip", 'User')
```

Luk terminalen og åbn en ny, så PATH bliver opdateret.

### 4.2 Lange stier

```powershell
git config --global core.longpaths true
```

Workspace'et ligger i `C:\zp` for at holde stierne korte. Windows' grænse på
260 tegn (MAX_PATH) kan ellers give mærkelige build-fejl i dybe modulmapper.

### 4.3 Python venv og west

```powershell
cd C:\zp
python -m venv C:\zp\.venv
C:\zp\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip west
```

Husk at aktivere venv'et (`C:\zp\.venv\Scripts\Activate.ps1`) i hver ny terminal.
Scripts i `scripts\` gør det automatisk.

### 4.4 West-workspace (T2)

```powershell
cd C:\zp
west init -l C:\zp\zephyr_a                 # repoet er manifestet
west update --narrow -o=--depth=1           # henter Zephyr v4.4.0 + 5 moduler
west config zephyr.base-prefer configfile   # ignorér globale ZEPHYR_BASE
west zephyr-export                          # registrerer Zephyr som CMake-pakke
west list                                   # kontrol: zephyr v4.4.0 + 5 moduler
```

### 4.5 Python-krav

Kommandoen for PowerShell fra 4.4.0-guiden. `west packages pip` samler både
Zephyrs krav og modulernes krav (fx `esptool` fra hal_espressif og `imgtool`-krav
fra MCUboot):

```powershell
cd C:\zp
python -m pip install @((west packages pip) -split ' ')
```

### 4.6 Zephyr SDK 1.0.1 (kun RISC-V)

Findes `C:\Users\<dig>\zephyr-sdk-1.0.1\gnu\riscv64-zephyr-elf` allerede, skal den
bare registreres:

```powershell
& "$env:USERPROFILE\zephyr-sdk-1.0.1\setup.cmd" /c
```

Ellers installeres den med:

```powershell
cd C:\zp\zephyr
west sdk install -t riscv64-zephyr-elf
```

`west sdk install` læser versionen fra `zephyr\SDK_VERSION` (1.0.1) og installerer
i `%USERPROFILE%\zephyr-sdk-1.0.1`.

### 4.7 Espressif binary blobs

```powershell
cd C:\zp
west blobs fetch hal_espressif --auto-accept
```

Blobs er lukkede biblioteker fra Espressif (Wi-Fi/BLE/PHY-kalibrering).

---

## 5. Boardet: USB-porte, COM-port og driver

ESP32-C6-DevKitC-1 har **to USB-C-porte**:

| Port (silketryk) | Forbundet til | Brug i dette projekt |
|---|---|---|
| **UART** | USB-til-UART-bro-chip → ESP32-C6 **UART0** (GPIO16 TX / GPIO17 RX) | **Flash og console/log.** Zephyrs console er `uart0` (`zephyr,console = &uart0` i board-dts) |
| **USB** | ESP32-C6's indbyggede **USB Serial/JTAG** (GPIO12/13) | Kan flashe og bruges til JTAG-debug, men Zephyr-loggen kommer **ikke** her som standard |

**Brug UART-porten.** Den kan også auto-resette chippen ind i download-mode, så du
ikke skal trykke på BOOT/RESET-knapperne.

### Find COM-porten

```powershell
Get-CimInstance Win32_PnPEntity | Where-Object Name -match '\(COM\d+\)' | Select-Object Name
```

Eller: Enhedshåndtering (Device Manager) → *Porte (COM og LPT)*. Træk kablet ud og
sæt det i igen for at se, hvilken port der kommer og går.

### Driver

- **UART-porten:** Hvilken bro-chip der sidder på boardet, afhænger af revisionen.
  Windows 11 installerer normalt driveren automatisk. Vises enheden i
  Enhedshåndteringen med gult udråbstegn, så se chip-navnet under *Andre enheder*
  og installér producentens VCP-driver (fx Silicon Labs CP210x eller WCH CH34x).
- **USB-porten:** Bruger Windows' indbyggede driver (vises som "USB Serial Device").

---

## 6. Byg, flash og monitor

```powershell
cd C:\zp\zephyr_a
.\scripts\build.ps1                    # = west build --sysbuild -p auto -b esp32c6_devkitc/esp32c6/hpcore -S espressif-flash-4M app -d build
.\scripts\flash.ps1   -Port COM5       # = west flash -d build (MCUboot først, så app)
.\scripts\monitor.ps1 -Port COM5       # = west espressif monitor (afslut: Ctrl+T og derefter X)
```

| Script | Parametre |
|---|---|
| `build.ps1` | `-Pristine` (ren genopbygning), `-Board <target>`, `-FlashSize 4M` (standard; se `docs/hardware.md`), `-ExtraConf app\secrets.conf` |
| `flash.ps1` | `-Port COMx` (krævet), `-Baud 115200` (hvis flash fejler) |
| `monitor.ps1` | `-Port COMx` (krævet), `-Simple` (pyserial miniterm i stedet for idf_monitor) |

### Board target

Zephyr 4.4.0 bruger hardware model v2 med board/SoC/kerne:

| Kerne | Board target |
|---|---|
| HP-kerne (hovedapplikation) | `esp32c6_devkitc/esp32c6/hpcore` |
| LP-kerne (ULP, `remote/`) | `esp32c6_devkitc/esp32c6/lpcore` |

Kontrol:

```powershell
west boards | Select-String esp32c6
# esp32c6_devkitc er boardet; kernerne ses i
# C:\zp\zephyr\boards\espressif\esp32c6_devkitc\esp32c6_devkitc_hpcore.yaml / _lpcore.yaml
```

### Forventet output i monitoren

```
*** Booting Zephyr OS build <git-hash eller v4.4.0> ***
[00:00:00.012,000] <inf> app_main: boot
[00:00:00.012,000] <inf> app_main: Device id: esp32c6-xxxxxxxxxxxx
[00:00:00.012,000] <inf> app_main: Zephyr version: 4.4.0
[00:00:00.012,000] <inf> app_main: App version: 0.1.0-del01 (board esp32c6_devkitc/esp32c6/hpcore)
[00:00:00.012,000] <inf> app_main: Reset cause: power-on (0x00000001)
[00:00:00.012,000] <inf> app_main: heartbeat #1 device=esp32c6-xxxxxxxxxxxx zephyr=4.4.0 uptime=12 ms
[00:00:05.012,000] <inf> app_main: heartbeat #2 ...
```

Før Zephyr-loggen kommer der et par linjer fra ROM-bootloaderen og MCUboot. Det er
normalt.

Kommer der en `W (spi_flash): Detected size(...) smaller than ...`-linje, passer
`-FlashSize` ikke til boardet. Projektets board har 4 MB (standard i `build.ps1`).

---

## 7. Fejlfinding

| Symptom | Løsning |
|---|---|
| `Activate.ps1 cannot be loaded because running scripts is disabled` | `Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser` |
| `python` åbner Microsoft Store | Installér `Python.Python.3.12` med winget, og slå app-aliaset fra: Indstillinger → Apps → Avancerede appindstillinger → App-eksekveringsaliasser → python.exe *Fra* |
| `cmake`/`ninja` not found lige efter installation | Luk og åbn terminalen (PATH opdateres kun i nye terminaler) |
| `esptool>=5.0.2 not found in PATH` | Python-kravene mangler: kør trin 4.5 i det aktiverede venv |
| `west sdk install`: fejl om `7z`/patool | 7-Zip mangler i PATH (trin 4.1) |
| `Filename too long` under `west update` | `git config --global core.longpaths true`, og brug `C:\zp` |
| Build bruger forkert Zephyr | `west config zephyr.base-prefer configfile`, og tjek `west topdir` = `C:\zp` |
| Flash: `Failed to connect` / timeout | Brug **UART**-porten, prøv `-Baud 115200`, eller hold **BOOT** nede og tryk **RESET** for at gå i download-mode |
| Flash: `could not open port ... Access is denied` | Porten er optaget af monitoren eller et andet program. Luk det først |
| Monitoren kan ikke lukkes med Ctrl+] | På dansk tastatur kræver `]` AltGr. Brug **Ctrl+T og derefter X**. Lukker du bare vinduet, sker der ikke noget med boardet |
| Monitor viser ulæselige tegn | Forkert baudrate: console er 115200 |
| Reset-årsag `brownout` | Modemets strømspidser – se `docs/hardware.md` |

---

## 8. Valgfrit: devcontainer

Kravet tillader også en VS Code devcontainer. Projektet bruger Windows 11 native som
primært miljø, men `.devcontainer/` er med som alternativ:

- Image: `ghcr.io/zephyrproject-rtos/zephyr-build:v0.29.1`. Det er den docker-image-
  version, Zephyr v4.4.0's egen CI bruger (`.github/workflows/twister.yaml` →
  `ci-repo-cache:v0.29.1...`), så den passende SDK følger med i imaget.
- Workspace (`zephyr/`, `modules/`) ligger i et Docker-volume, og repoet bindes ind som
  manifest-repo. `post-create.sh` kører `west init -l`, `west update` osv.
- Kræver Docker Desktop (WSL 2-backend) og VS Code-udvidelsen *Dev Containers*.

**Flash fra Windows-værten.** Docker på Windows kan ikke se USB-enheder uden
`usbipd-win` (USB/IP-videresendelse til WSL). Det er nemmere at bygge i containeren og
flashe fra Windows med det native miljø:

```powershell
# I en almindelig Windows-terminal, efter build i containeren:
cd C:\zp\zephyr_a
.\scripts\flash.ps1 -Port COM5
```

Build-mappen `build\` ligger i det bind-mountede repo og kan derfor ses fra Windows.
Devcontaineren er **ikke testet** i del 1. Det native Windows-miljø er det primære.

---

## 9. Kilder

- Zephyr v4.4.0: `doc/develop/getting_started/index.rst` (winget-liste, venv, `west packages pip`, `west sdk install`)
- Zephyr v4.4.0: `SDK_VERSION` (= 1.0.1)
- Zephyr v4.4.0: `boards/espressif/esp32c6_devkitc/` (board targets, console på `uart0`, MCUboot-standard i `Kconfig.sysbuild`)
- Zephyr v4.4.0: `boards/espressif/common/system-requirements.rst` (`west blobs fetch hal_espressif`)
- Zephyr v4.4.0: `samples/boards/espressif/ulp/lp_core/*` (sysbuild-struktur med `remote/`)
- Zephyr v4.4.0: `drivers/hwinfo/hwinfo_esp32.c` (device-id = MAC fra eFuse)
- Espressif: ESP32-C6-DevKitC-1 v1.2 User Guide (USB-porte, pin headers)
- Online-dokumentation for versionen: https://docs.zephyrproject.org/4.4.0/
