<#
.SYNOPSIS
    Builds the Zephyr_A firmware (sysbuild: MCUboot + HP-core app).

.DESCRIPTION
    Runs: west build --sysbuild -p auto -b <board> -S espressif-flash-4M app -d build
    Sysbuild is used because the project builds several images (MCUboot, the
    HP-core application and later the LP-core image in app\remote).

    Build variants (can be combined):
      -Simulation  adds app\overlay-sim.conf (simulated values, no hardware)
      -ModbusRtu   adds app\overlay-modbus-rtu.conf + app\modbus-rtu.overlay
                   (RS485 on UART0, console moves to the "USB" port)
      -ModemDebug  adds app\overlay-modem-debug.conf (shell with modem AT
                   commands + debug logs from CMUX/PPP/chat)
      -TlsDebug    adds app\overlay-tls-debug.conf (Mbed TLS handshake log,
                   TLS heap statistics, DEBUG log of the app)
      -Release     adds app\overlay-release.conf (no shell/debug, fewer logs);
                   not together with -ModemDebug/-TlsDebug
      -LowPower    adds app\overlay-lowpower.conf (interval uplink every
                   30 min, modem radio off and light sleep in between)

    Firmware signing (part 8): if certs\mcuboot-signing-key.pem exists
    (scripts\new-signing-key.ps1), MCUboot is built with its public key and
    the application is signed with it; afterwards the signature is checked
    with imgtool. Without the key MCUboot's public DEVELOPMENT key is used
    (warning).

    -Sbom writes an SPDX 2.3 software bill of materials to build\spdx
    (west spdx; lists Zephyr, Mbed TLS, TF-PSA-Crypto, nanopb ... with versions).

    Secrets: app\secrets.conf (MQTT password, ignored by git) is added
    automatically when it exists. Without it the firmware builds, but the
    MQTT client refuses to start.

    CMake caches -D options, so switching variant with an incremental build
    would silently keep the old variant. The script remembers the variant in
    build\zephyr_a_variant.txt and forces a pristine build when it changes.

.PARAMETER Board
    Board target. Default: esp32c6_devkitc/esp32c6/hpcore

.PARAMETER FlashSize
    SPI flash size of the module, applied with Espressif's board-variant
    snippet "espressif-flash-<size>" (Zephyr 4.4.0 snippets/espressif).
    Default 4M: the project board reports "Detected size(4096k)" at boot,
    while the board default devicetree assumes 8 MB. Sysbuild applies the
    snippet to all images (app and MCUboot). See docs/hardware.md.

.PARAMETER Pristine
    Force a clean rebuild (-p always) instead of -p auto.

.PARAMETER Simulation
    Generate simulated measurement values (CONFIG_APP_SENSOR_SIMULATION).

.PARAMETER ModbusRtu
    Enable the Modbus RTU client (RS485 on UART0, console on USB Serial/JTAG).

.PARAMETER ModemDebug
    Shell with "modem send AT..." commands and verbose modem logs.

.PARAMETER TlsDebug
    Mbed TLS debug log and heap statistics (troubleshooting the broker connection).

.PARAMETER Release
    Release variant: no shell or debug output (app\overlay-release.conf).

.PARAMETER LowPower
    Low-power variant: interval uplink (app\overlay-lowpower.conf, docs\09-strom-data.md).

.PARAMETER Sbom
    Generate an SPDX SBOM in build\spdx after the build.

.PARAMETER ExtraConf
    Extra Kconfig fragment(s). app\secrets.conf is added automatically.

.EXAMPLE
    .\scripts\build.ps1
    .\scripts\build.ps1 -Simulation
    .\scripts\build.ps1 -ModbusRtu
    .\scripts\build.ps1 -ModbusRtu -Simulation
    .\scripts\build.ps1 -ModemDebug
    .\scripts\build.ps1 -TlsDebug
    .\scripts\build.ps1 -Release -Sbom
    .\scripts\build.ps1 -LowPower
    .\scripts\build.ps1 -LowPower -Release
#>
[CmdletBinding()]
param(
    [string]$Board,
    [ValidateSet('4M', '8M', '16M')]
    [string]$FlashSize = '4M',
    [switch]$Pristine,
    [switch]$Simulation,
    [switch]$ModbusRtu,
    [switch]$ModemDebug,
    [switch]$TlsDebug,
    [switch]$Release,
    [switch]$LowPower,
    [switch]$Sbom,
    [string[]]$ExtraConf
)

. (Join-Path $PSScriptRoot 'env.ps1')
Enable-Venv

if (-not $Board) { $Board = $DefaultBoard }

function ConvertTo-CMakePath([string]$Path) {
    return ((Resolve-Path $Path).Path -replace '\\', '/')
}

$confFiles = @()
$dtcFiles = @()
if ($ModbusRtu) {
    $confFiles += ConvertTo-CMakePath (Join-Path $AppDir 'overlay-modbus-rtu.conf')
    $dtcFiles += ConvertTo-CMakePath (Join-Path $AppDir 'modbus-rtu.overlay')
}
if ($Simulation) {
    $confFiles += ConvertTo-CMakePath (Join-Path $AppDir 'overlay-sim.conf')
}
if ($ModemDebug) {
    $confFiles += ConvertTo-CMakePath (Join-Path $AppDir 'overlay-modem-debug.conf')
}
if ($TlsDebug) {
    $confFiles += ConvertTo-CMakePath (Join-Path $AppDir 'overlay-tls-debug.conf')
}
if ($Release) {
    if ($TlsDebug -or $ModemDebug) {
        throw '-Release cannot be combined with -TlsDebug or -ModemDebug'
    }
    $confFiles += ConvertTo-CMakePath (Join-Path $AppDir 'overlay-release.conf')
}
if ($LowPower) {
    $confFiles += ConvertTo-CMakePath (Join-Path $AppDir 'overlay-lowpower.conf')
}
foreach ($f in $ExtraConf) {
    $confFiles += ConvertTo-CMakePath $f
}
# Secrets last, so they win over every other fragment.
$secrets = Join-Path $AppDir 'secrets.conf'
if (Test-Path $secrets) {
    $secretsPath = ConvertTo-CMakePath $secrets
    if ($confFiles -notcontains $secretsPath) { $confFiles += $secretsPath }
    Write-Step 'Using app\secrets.conf'
} else {
    Write-Warn2 'app\secrets.conf not found - the MQTT client will not start (see app\secrets.conf.example)'
}

# Firmware signing key (part 8). Passed as a sysbuild Kconfig fragment, not as
# -DSB_CONFIG_...="path": Windows PowerShell 5.1 strips the inner quotes that a
# Kconfig string needs.
$signKey = Join-Path $RepoRoot 'certs\mcuboot-signing-key.pem'
$sbConf = ''
if (Test-Path $signKey) {
    $sbConfFile = Join-Path $RepoRoot 'certs\sysbuild-signing.conf'
    Set-Content -Path $sbConfFile -Encoding ascii -Value @(
        '# Generated by scripts\build.ps1 - do not edit, not in git',
        "SB_CONFIG_BOOT_SIGNATURE_KEY_FILE=`"$(ConvertTo-CMakePath $signKey)`"")
    $sbConf = ConvertTo-CMakePath $sbConfFile
    Write-Step 'Signing with certs\mcuboot-signing-key.pem (ECDSA P-256)'
} else {
    Write-Warn2 ('No certs\mcuboot-signing-key.pem - using MCUboot''s PUBLIC development key. ' +
                 'Anyone can sign images for this device. Run .\scripts\new-signing-key.ps1')
}

# Variant fingerprint -> pristine build when it changes (CMake cache is sticky).
$variant = "board=$Board;flash=$FlashSize;conf=$($confFiles -join ',');dtc=$($dtcFiles -join ',');sign=$sbConf"
$variantFile = Join-Path $BuildDir 'zephyr_a_variant.txt'
$pristineMode = 'auto'
if ($Pristine) {
    $pristineMode = 'always'
} elseif (Test-Path $variantFile) {
    if ((Get-Content $variantFile -Raw).Trim() -ne $variant) {
        Write-Warn2 'Build variant changed - forcing a pristine build'
        $pristineMode = 'always'
    }
} elseif (Test-Path $BuildDir) {
    $pristineMode = 'always'
}

$westArgs = @('build', '--sysbuild', '-p', $pristineMode, '-b', $Board,
              '-S', "espressif-flash-$FlashSize", $AppDir, '-d', $BuildDir, '--')
# Always pass both options (empty = none) so no old value survives in the cache.
$westArgs += "-DEXTRA_CONF_FILE=$($confFiles -join ';')"
$westArgs += "-DEXTRA_DTC_OVERLAY_FILE=$($dtcFiles -join ';')"
$westArgs += "-DSB_EXTRA_CONF_FILE=$sbConf"

Write-Step "west $($westArgs -join ' ')"
Push-Location $RepoRoot
try {
    & west @westArgs
    Assert-LastExit 'west build'
} finally {
    Pop-Location
}

Set-Content -Path $variantFile -Value $variant -Encoding ascii

# Check the signature of the image MCUboot will boot (part 8).
$imgtool = Join-Path $WorkspaceRoot 'bootloader\mcuboot\scripts\imgtool.py'
$signed = Join-Path $BuildDir 'app\zephyr\zephyr.signed.bin'
if ((Test-Path $signKey) -and (Test-Path $signed)) {
    & python $imgtool verify -k $signKey $signed
    Assert-LastExit 'imgtool verify (signature check)'
    Write-Ok 'Signature verified with certs\mcuboot-signing-key.pem'
}

if ($Sbom) {
    # west spdx needs CMake's file API reply: register the query, re-run the
    # app's CMake configure step (no rebuild) and generate the SPDX documents.
    $appBuild = Join-Path $BuildDir 'app'
    $spdxDir = Join-Path $BuildDir 'spdx'
    Push-Location $RepoRoot
    try {
        & west spdx --init -d $appBuild
        Assert-LastExit 'west spdx --init'
        & cmake $appBuild | Out-Null
        Assert-LastExit 'cmake (re-configure for SBOM)'
        & west spdx -d $appBuild -s $spdxDir
        Assert-LastExit 'west spdx'
    } finally {
        Pop-Location
    }
    Write-Ok "SBOM (SPDX 2.3): $spdxDir (modules-deps.spdx lists the third-party components)"
}
$desc = @()
if ($Simulation) { $desc += 'simulation' }
if ($ModbusRtu) { $desc += 'Modbus RTU (console on USB port)' }
if ($ModemDebug) { $desc += 'modem debug shell' }
if ($TlsDebug) { $desc += 'TLS debug' }
if ($Release) { $desc += 'release' }
if ($LowPower) { $desc += 'low power (interval uplink)' }
if ($desc.Count -eq 0) { $desc += 'standard' }
Write-Ok "Build OK ($($desc -join ', ')). Output: $BuildDir\app\zephyr\zephyr.bin (+ $BuildDir\mcuboot)"
