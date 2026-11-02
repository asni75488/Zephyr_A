<#
.SYNOPSIS
    Shared environment for the Zephyr_A PowerShell scripts (dot-source it).

.DESCRIPTION
    Resolves the repository root and the west workspace root (the parent
    folder of the repository, e.g. C:\zp), activates the Python virtual
    environment in <workspace>\.venv and defines common helpers.
    Compatible with Windows PowerShell 5.1 and PowerShell 7.
#>

$ErrorActionPreference = 'Stop'

$script:RepoRoot      = Split-Path -Parent $PSScriptRoot
$script:WorkspaceRoot = Split-Path -Parent $RepoRoot
$script:VenvDir       = Join-Path $WorkspaceRoot '.venv'
$script:BuildDir      = Join-Path $RepoRoot 'build'
$script:AppDir        = Join-Path $RepoRoot 'app'

# Board target for the ESP32-C6-DevKitC high-performance (HP) core.
# Verified in Zephyr 4.4.0: boards/espressif/esp32c6_devkitc/esp32c6_devkitc_hpcore.yaml
$script:DefaultBoard = 'esp32c6_devkitc/esp32c6/hpcore'

function Write-Step([string]$Message) {
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Ok([string]$Message) {
    Write-Host "    [ok] $Message" -ForegroundColor Green
}

function Write-Warn2([string]$Message) {
    Write-Host "    [warn] $Message" -ForegroundColor Yellow
}

function Assert-LastExit([string]$What) {
    if ($LASTEXITCODE -ne 0) {
        throw "$What failed (exit code $LASTEXITCODE)"
    }
}

function Enable-Venv {
    $activate = Join-Path $VenvDir 'Scripts\Activate.ps1'
    if (-not (Test-Path $activate)) {
        throw "Python venv not found at $VenvDir. Run scripts\setup.ps1 first."
    }
    if ($env:VIRTUAL_ENV -ne $VenvDir) {
        . $activate
    }
}

function Get-SerialPorts {
    # Lists COM ports with their device names, e.g.
    # "Silicon Labs CP210x USB to UART Bridge (COM5)" or
    # "USB Serial Device (COM6)" (ESP32-C6 native USB Serial/JTAG).
    Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '\(COM\d+\)' } |
        Sort-Object Name |
        Select-Object -ExpandProperty Name
}

function Show-SerialPorts {
    $ports = @(Get-SerialPorts)
    if ($ports.Count -eq 0) {
        Write-Host '    No COM ports found. Is the board connected with a data cable?'
    } else {
        Write-Host '    Available COM ports:'
        foreach ($p in $ports) { Write-Host "      $p" }
    }
}

function Assert-Port([string]$Port) {
    if ([string]::IsNullOrWhiteSpace($Port)) {
        Write-Host 'Missing -Port. Use the COM port of the "UART" USB-C connector.' -ForegroundColor Yellow
        Show-SerialPorts
        throw 'Specify the port, e.g. -Port COM5'
    }
    if ($Port -notmatch '^COM\d+$') {
        throw "Invalid port '$Port'. Expected something like COM5."
    }
}
