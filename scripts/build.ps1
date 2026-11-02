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

.PARAMETER ExtraConf
    Extra Kconfig fragment(s), e.g. app\secrets.conf.

.EXAMPLE
    .\scripts\build.ps1
    .\scripts\build.ps1 -Simulation
    .\scripts\build.ps1 -ModbusRtu
    .\scripts\build.ps1 -ModbusRtu -Simulation
#>
[CmdletBinding()]
param(
    [string]$Board,
    [ValidateSet('4M', '8M', '16M')]
    [string]$FlashSize = '4M',
    [switch]$Pristine,
    [switch]$Simulation,
    [switch]$ModbusRtu,
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
foreach ($f in $ExtraConf) {
    $confFiles += ConvertTo-CMakePath $f
}

# Variant fingerprint -> pristine build when it changes (CMake cache is sticky).
$variant = "board=$Board;flash=$FlashSize;conf=$($confFiles -join ',');dtc=$($dtcFiles -join ',')"
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

Write-Step "west $($westArgs -join ' ')"
Push-Location $RepoRoot
try {
    & west @westArgs
    Assert-LastExit 'west build'
} finally {
    Pop-Location
}

Set-Content -Path $variantFile -Value $variant -Encoding ascii
$desc = @()
if ($Simulation) { $desc += 'simulation' }
if ($ModbusRtu) { $desc += 'Modbus RTU (console on USB port)' }
if ($desc.Count -eq 0) { $desc += 'standard' }
Write-Ok "Build OK ($($desc -join ', ')). Output: $BuildDir\app\zephyr\zephyr.bin (+ $BuildDir\mcuboot)"
