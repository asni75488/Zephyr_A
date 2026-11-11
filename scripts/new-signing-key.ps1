<#
.SYNOPSIS
    Creates the firmware signing key (ECDSA P-256) for MCUboot.

.DESCRIPTION
    Writes certs\mcuboot-signing-key.pem (PRIVATE, ignored by git) and
    certs\mcuboot-signing-key.pub.pem with MCUboot's imgtool from the west
    workspace. scripts\build.ps1 uses the key automatically from then on:
    MCUboot gets the public key compiled in, and the application image is
    signed with the private key.

    Run once per project. Keep a backup of the private key (see certs\README.md).
    An existing key is never overwritten.

.EXAMPLE
    .\scripts\new-signing-key.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1')
Enable-Venv

$certs = Join-Path $RepoRoot 'certs'
$key = Join-Path $certs 'mcuboot-signing-key.pem'
$pub = Join-Path $certs 'mcuboot-signing-key.pub.pem'
$imgtool = Join-Path $WorkspaceRoot 'bootloader\mcuboot\scripts\imgtool.py'

if (Test-Path $key) {
    Write-Warn2 "$key already exists - not overwritten (delete it yourself if you really want a new key)"
    exit 1
}
if (-not (Test-Path $imgtool)) {
    throw "imgtool not found: $imgtool (run 'west update')"
}

New-Item -ItemType Directory -Force -Path $certs | Out-Null
Write-Step 'Generating ECDSA P-256 signing key'
& python $imgtool keygen -k $key -t ecdsa-p256
Assert-LastExit 'imgtool keygen'
& python $imgtool getpub -k $key -e pem | Set-Content -Path $pub -Encoding ascii
Assert-LastExit 'imgtool getpub'

# The private key must stay out of git (certs/* is ignored).
& git -C $RepoRoot check-ignore -q $key
if ($LASTEXITCODE -ne 0) {
    throw "$key is NOT ignored by git - check .gitignore before continuing"
}

Write-Ok "Private key: $key  (secret, ignored by git - make a backup)"
Write-Ok "Public key:  $pub"
Write-Step 'Next: .\scripts\build.ps1 -Pristine   then   .\scripts\flash.ps1 (flashes MCUboot with the new public key)'
