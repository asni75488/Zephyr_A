<#
.SYNOPSIS
    Builds the Zephyr_A firmware (sysbuild: MCUboot + HP-core app).

.DESCRIPTION
    Runs: west build --sysbuild -p auto -b <board> app -d build
    Sysbuild is used because the project builds several images (MCUboot, the
    HP-core application and later the LP-core image in app\remote).

.PARAMETER Board
    Board target. Default: esp32c6_devkitc/esp32c6/hpcore

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
    [switch]$Pristine,
    [string[]]$ExtraConf
)

. (Join-Path $PSScriptRoot 'env.ps1')
Enable-Venv

if (-not $Board) { $Board = $DefaultBoard }
$pristineMode = 'auto'
if ($Pristine) { $pristineMode = 'always' }

$westArgs = @('build', '--sysbuild', '-p', $pristineMode, '-b', $Board, $AppDir, '-d', $BuildDir)

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
