<#
.SYNOPSIS
    Builds the Zephyr_A firmware (sysbuild: MCUboot + HP-core app).

.DESCRIPTION
    Runs: west build --sysbuild -p auto -b <board> -S espressif-flash-4M app -d build
    Sysbuild is used because the project builds several images (MCUboot, the
    HP-core application and later the LP-core image in app\remote).

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

.PARAMETER ExtraConf
    Extra Kconfig fragment(s), e.g. app\secrets.conf (used from part 3 on).

.EXAMPLE
    .\scripts\build.ps1
    .\scripts\build.ps1 -Pristine
#>
[CmdletBinding()]
param(
    [string]$Board,
    [ValidateSet('4M', '8M', '16M')]
    [string]$FlashSize = '4M',
    [switch]$Pristine,
    [string[]]$ExtraConf
)

. (Join-Path $PSScriptRoot 'env.ps1')
Enable-Venv

if (-not $Board) { $Board = $DefaultBoard }
$pristineMode = 'auto'
if ($Pristine) { $pristineMode = 'always' }

$westArgs = @('build', '--sysbuild', '-p', $pristineMode, '-b', $Board,
              '-S', "espressif-flash-$FlashSize", $AppDir, '-d', $BuildDir)

if ($ExtraConf) {
    $files = @()
    foreach ($f in $ExtraConf) {
        $full = (Resolve-Path $f).Path -replace '\\', '/'
        $files += $full
    }
    $westArgs += '--'
    $westArgs += "-DEXTRA_CONF_FILE=$($files -join ';')"
}

Write-Step "west $($westArgs -join ' ')"
Push-Location $RepoRoot
try {
    & west @westArgs
    Assert-LastExit 'west build'
} finally {
    Pop-Location
}
Write-Ok "Build OK. Output: $BuildDir\app\zephyr\zephyr.bin (+ $BuildDir\mcuboot)"
