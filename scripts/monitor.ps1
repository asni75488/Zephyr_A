<#
.SYNOPSIS
    Opens the serial console of the ESP32-C6-DevKitC (115200 8N1).

.DESCRIPTION
    Default: "west espressif monitor" (Espressif's idf_monitor from
    hal_espressif). It decodes nothing by default but resets the board on
    start so the boot log is visible.
    Exit: Ctrl+T, then X. (Ctrl+] also works on US keyboards, but on a Danish
    keyboard "]" needs AltGr, so Ctrl+] usually cannot be typed.)

    -Simple uses pyserial's miniterm instead (no reset). Exit: Ctrl+X.

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
        Write-Step "pyserial miniterm $Port $Baud (exit: Ctrl+X)"
        # --exit-char 24 = Ctrl+X (the default Ctrl+] is hard to type on Danish keyboards)
        & python -m serial.tools.miniterm --raw --eol CRLF --exit-char 24 $Port $Baud
    } else {
        $elf = Join-Path $BuildDir 'app\zephyr\zephyr.elf'
        if (-not (Test-Path $elf)) {
            throw "ELF not found: $elf. Run .\scripts\build.ps1 first (or use -Simple)."
        }
        Write-Step "west espressif monitor -p $Port (exit: Ctrl+T, then X)"
        & west espressif monitor -p $Port -b $Baud -e $elf
    }
} finally {
    Pop-Location
}
