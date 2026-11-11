<#
.SYNOPSIS
    Flashes MCUboot + the application to the ESP32-C6-DevKitC.

.DESCRIPTION
    Runs west flash on the sysbuild build directory. West flashes the images
    in the order given in build\domains.yaml (mcuboot first, then app).
    The esp32 runner reads the serial port from ESPTOOL_PORT, so the same port
    is used for all images.

.PARAMETER Port
    COM port of the board, e.g. COM5. Use the "UART" USB-C connector.

.PARAMETER AppOnly
    Flash only the application image (sysbuild domain "app"), not MCUboot.
    Used for the signature test in docs/08-sikkerhed.md: MCUboot keeps its
    public key and must refuse an image signed with another key.

.PARAMETER Baud
    Optional flash baud rate. The runner default is 921600; try 460800 or
    115200 if flashing fails with long/poor USB cables.

.EXAMPLE
    .\scripts\flash.ps1 -Port COM5
    .\scripts\flash.ps1 -Port COM5 -Baud 115200
    .\scripts\flash.ps1 -Port COM5 -AppOnly
#>
[CmdletBinding()]
param(
    [string]$Port,
    [string]$Baud,
    [switch]$AppOnly
)

. (Join-Path $PSScriptRoot 'env.ps1')
Assert-Port $Port
Enable-Venv

if (-not (Test-Path (Join-Path $BuildDir 'domains.yaml'))) {
    throw "No sysbuild build found in $BuildDir. Run .\scripts\build.ps1 first."
}

$env:ESPTOOL_PORT = $Port
$westArgs = @('flash', '-d', $BuildDir)
if ($AppOnly) {
    $westArgs += @('--domain', 'app')
}
if ($Baud) {
    # Runner options apply to every sysbuild domain (west prints a warning).
    $westArgs += @('--esp-baud-rate', $Baud)
}

Write-Step "west $($westArgs -join ' ')  (port $Port)"
Push-Location $RepoRoot
try {
    & west @westArgs
    Assert-LastExit 'west flash'
} finally {
    Pop-Location
    Remove-Item Env:\ESPTOOL_PORT -ErrorAction SilentlyContinue
}
Write-Ok "Flashed. Open the monitor: .\scripts\monitor.ps1 -Port $Port"
