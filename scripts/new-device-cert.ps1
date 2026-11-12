<#
.SYNOPSIS
    Issues (or revokes) the client certificate of a device for mTLS (part 10).

.DESCRIPTION
    Wrapper around scripts\device_pki.py. Creates the device CA in
    certs\device-pki the first time (PRIVATE, ignored by git - keep a backup
    of certs\device-pki\ca.key offline), then a key + certificate with
    CN = device id. The device id is printed at boot ("Device id: ...").

    After a new CA or a revoke, copy ca.crt and crl.pem to the server and run
    cloud/scripts/65-mtls.sh (docs\10-nice-to-have.md, section 3).

.EXAMPLE
    .\scripts\new-device-cert.ps1 -DeviceId esp32c6-14c19fe0ab24
    .\scripts\new-device-cert.ps1 -DeviceId esp32c6-14c19fe0ab24 -Revoke
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$DeviceId,
    [switch]$Revoke
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1')
Enable-Venv

$pki = Join-Path $PSScriptRoot 'device_pki.py'
$caKey = Join-Path $RepoRoot 'certs\device-pki\ca.key'

if (-not (Test-Path $caKey)) {
    Write-Step 'Creating the device CA (once)'
    & python $pki init
    Assert-LastExit 'device_pki.py init'
}
& git -C $RepoRoot check-ignore -q $caKey
if ($LASTEXITCODE -ne 0) { throw "$caKey is NOT ignored by git - check .gitignore" }

if ($Revoke) {
    & python $pki revoke $DeviceId
    Assert-LastExit 'device_pki.py revoke'
    Write-Step 'Copy certs\device-pki\crl.pem to the server, then: sudo ./65-mtls.sh --crl ~/crl.pem'
} else {
    & python $pki issue $DeviceId
    Assert-LastExit 'device_pki.py issue'
    Write-Step "Next: .\scripts\build.ps1 -Mtls -DeviceId $DeviceId -Pristine; .\scripts\flash.ps1"
}
