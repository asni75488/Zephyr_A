<#
.SYNOPSIS
    Opens the serial console of the ESP32-C6-DevKitC (115200 8N1).

.DESCRIPTION
    Default: "west espressif monitor" (Espressif's idf_monitor from
    hal_espressif). It decodes nothing by default but resets the board on
    start so the boot log is visible. Exit with Ctrl+].

    -Simple uses pyserial's miniterm instead (no reset). Exit with Ctrl+].

.PARAMETER Port
    COM port of the "UART" USB-C connector, e.g. COM5.

.PARAMETER Simple
    Use pyserial miniterm instead of west espressif monitor.

.EXAMPLE
    .\scripts\monitor.ps1 -Port COM5
#>
[CmdletBinding()]
param(
    [string]$Port,
    [string]$Baud = '115200',
    [switch]$Simple
)

. (Join-Path $PSScriptRoot 'env.ps1')
Assert-Port $Port
Enable-Venv

Push-Location $RepoRoot
try {
    if ($Simple) {
        Write-Step "pyserial miniterm $Port $Baud (exit: Ctrl+])"
        & python -m serial.tools.miniterm --raw --eol CRLF $Port $Baud
    } else {
        $elf = Join-Path $BuildDir 'app\zephyr\zephyr.elf'
        if (-not (Test-Path $elf)) {
            throw "ELF not found: $elf. Run .\scripts\build.ps1 first (or use -Simple)."
        }
        Write-Step "west espressif monitor -p $Port (exit: Ctrl+])"
        & west espressif monitor -p $Port -b $Baud -e $elf
    }
} finally {
    Pop-Location
}
