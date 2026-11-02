<#
.SYNOPSIS
    Sets up the Zephyr 4.4.0 development environment for Zephyr_A on Windows 11.

.DESCRIPTION
    Idempotent: every step checks whether it is already done and skips it,
    so the script can be re-run safely (e.g. after a failed download).

    Expected layout (T2 west topology, the repository is the manifest):
        C:\zp\              <- west workspace (parent of the repository)
        C:\zp\zephyr_a\     <- this repository (clone it here)
        C:\zp\.venv\        <- Python virtual environment
        C:\zp\zephyr\       <- Zephyr v4.4.0 (created by west update)
        C:\zp\modules\...   <- allowlisted modules (west.yml)

    Steps:
      1. Host tools via winget (official Zephyr 4.4.0 Getting Started list)
      2. git core.longpaths
      3. Python venv + west
      4. west init -l / west update / west zephyr-export
      5. Python requirements (west packages pip)
      6. Zephyr SDK (version from zephyr\SDK_VERSION, RISC-V toolchain only)
      7. Espressif binary blobs

.PARAMETER SkipWinget
    Do not install/check host tools with winget.

.PARAMETER FullClone
    Use full git clones in west update (default: shallow, saves time/disk).

.EXAMPLE
    cd C:\zp\zephyr_a
    .\scripts\setup.ps1
#>
[CmdletBinding()]
param(
    [switch]$SkipWinget,
    [switch]$FullClone
)

. (Join-Path $PSScriptRoot 'env.ps1')

function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user    = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
}

function Add-UserPath([string]$Dir) {
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($null -eq $user) { $user = '' }
    $parts = $user.Split(';') | Where-Object { $_ -ne '' }
    if ($parts -notcontains $Dir) {
        [Environment]::SetEnvironmentVariable('Path', (($parts + $Dir) -join ';'), 'User')
        Write-Ok "Added $Dir to user PATH"
    }
    Update-SessionPath
}

function Test-RealPython {
    # "python" may be the Microsoft Store alias, which only opens the Store.
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if (-not $cmd) { return $false }
    if ($cmd.Source -like '*WindowsApps*') { return $false }
    & python -c "import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)" 2>$null
    return ($LASTEXITCODE -eq 0)
}

# ---------------------------------------------------------------------------
Write-Step "Workspace: $WorkspaceRoot   Repository: $RepoRoot"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($isAdmin) {
    Write-Warn2 'Running as Administrator. The Zephyr guide recommends a regular user terminal.'
}
if ($WorkspaceRoot.Length -gt 12) {
    Write-Warn2 "Workspace path '$WorkspaceRoot' is long. A short path like C:\zp avoids MAX_PATH problems."
}

# --- 1. Host tools -----------------------------------------------------------
if (-not $SkipWinget) {
    Write-Step 'Host tools (winget)'
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw 'winget not found. Install "App Installer" from Microsoft Store: https://aka.ms/getwinget'
    }

    # Package ids from Zephyr 4.4.0 doc/develop/getting_started/index.rst:
    #   winget install Kitware.CMake Ninja-build.Ninja oss-winget.gperf
    #     Python.Python.3.12 Git.Git oss-winget.dtc wget 7zip.7zip
    $tools = @(
        @{ Id = 'Kitware.CMake';      Test = { Get-Command cmake -ErrorAction SilentlyContinue } },
        @{ Id = 'Ninja-build.Ninja';  Test = { Get-Command ninja -ErrorAction SilentlyContinue } },
        @{ Id = 'oss-winget.gperf';   Test = { Get-Command gperf -ErrorAction SilentlyContinue } },
        @{ Id = 'Python.Python.3.12'; Test = { Test-RealPython } },
        @{ Id = 'Git.Git';            Test = { Get-Command git -ErrorAction SilentlyContinue } },
        @{ Id = 'oss-winget.dtc';     Test = { Get-Command dtc -ErrorAction SilentlyContinue } },
        @{ Id = 'wget';               Test = { Get-Command wget.exe -ErrorAction SilentlyContinue } },
        @{ Id = '7zip.7zip';          Test = { (Get-Command 7z -ErrorAction SilentlyContinue) -or
                                               (Test-Path (Join-Path $env:ProgramFiles '7-Zip\7z.exe')) } }
    )

    foreach ($t in $tools) {
        if (& $t.Test) {
            Write-Ok "$($t.Id) already installed"
            continue
        }
        Write-Host "    installing $($t.Id) ..."
        if ($t.Id -eq 'wget') {
            # "wget" is used as a search term in the official guide (no exact id).
            & winget install wget --accept-source-agreements --accept-package-agreements
        } else {
            & winget install --exact --id $t.Id --accept-source-agreements --accept-package-agreements
        }
        if ($LASTEXITCODE -ne 0) {
            Write-Warn2 "winget returned $LASTEXITCODE for $($t.Id) (it may already be installed)."
        }
        Update-SessionPath
    }

    # 7-Zip does not add itself to PATH; west sdk install (patool) needs 7z.exe.
    $sevenZipDir = Join-Path $env:ProgramFiles '7-Zip'
    if ((Test-Path (Join-Path $sevenZipDir '7z.exe')) -and -not (Get-Command 7z -ErrorAction SilentlyContinue)) {
        Add-UserPath $sevenZipDir
    }
}

foreach ($cmd in @('cmake', 'ninja', 'git')) {
    if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) {
        throw "$cmd not found on PATH. Open a new terminal (PATH refresh) and re-run, or install it manually."
    }
}
if (-not (Test-RealPython)) {
    throw 'A real Python >= 3.10 is not on PATH (only the Microsoft Store alias?). Install Python.Python.3.12, open a new terminal and re-run.'
}
Write-Ok ("cmake: " + ((& cmake --version) | Select-Object -First 1))
Write-Ok ("python: " + (& python --version))

# --- 2. Git long paths -------------------------------------------------------
Write-Step 'git core.longpaths'
$lp = (& git config --global --get core.longpaths)
if ($lp -ne 'true') {
    & git config --global core.longpaths true
    Assert-LastExit 'git config core.longpaths'
}
Write-Ok 'core.longpaths = true'

# --- 3. Python venv + west ---------------------------------------------------
Write-Step "Python venv in $VenvDir"
if (-not (Test-Path (Join-Path $VenvDir 'Scripts\python.exe'))) {
    & python -m venv $VenvDir
    Assert-LastExit 'python -m venv'
    Write-Ok 'venv created'
} else {
    Write-Ok 'venv already exists'
}
Enable-Venv
& python -m pip install --upgrade pip west
Assert-LastExit 'pip install west'
Write-Ok ((& west --version) | Out-String).Trim()

# --- 4. West workspace -------------------------------------------------------
Write-Step 'West workspace (T2: this repository is the manifest)'
Push-Location $WorkspaceRoot
try {
    if (-not (Test-Path (Join-Path $WorkspaceRoot '.west'))) {
        & west init -l $RepoRoot
        Assert-LastExit 'west init -l'
        Write-Ok 'west init done'
    } else {
        Write-Ok '.west already exists (west init skipped)'
    }

    if ($FullClone) {
        & west update
    } else {
        & west update --narrow -o=--depth=1
    }
    Assert-LastExit 'west update'

    # Always use the Zephyr from this workspace, even if a global ZEPHYR_BASE
    # points to an older installation (e.g. C:\Users\<user>\zephyrproject).
    & west config zephyr.base-prefer configfile
    if ($env:ZEPHYR_BASE) {
        Write-Warn2 "ZEPHYR_BASE is set globally to '$($env:ZEPHYR_BASE)'. West now ignores it for this workspace (zephyr.base-prefer=configfile)."
    }

    & west zephyr-export
    Assert-LastExit 'west zephyr-export'

    # --- 5. Python requirements ---------------------------------------------
    Write-Step 'Python requirements (west packages pip)'
    # Same as the PowerShell line in the 4.4.0 guide. Also pulls in the module
    # requirements (hal_espressif -> esptool, mcuboot -> imgtool deps, nanopb).
    $reqArgs = ((& west packages pip) -split ' ') | Where-Object { $_ -ne '' }
    Assert-LastExit 'west packages pip'
    & python -m pip install @reqArgs
    Assert-LastExit 'pip install requirements'
    Write-Ok 'Python requirements installed'

    # --- 6. Zephyr SDK -------------------------------------------------------
    $sdkVersion = (Get-Content (Join-Path $WorkspaceRoot 'zephyr\SDK_VERSION') -Raw).Trim()
    Write-Step "Zephyr SDK $sdkVersion (required by zephyr\SDK_VERSION)"
    $sdkDir = Join-Path $env:USERPROFILE "zephyr-sdk-$sdkVersion"
    $riscv  = Join-Path $sdkDir 'gnu\riscv64-zephyr-elf'
    if (Test-Path $riscv) {
        Write-Ok "Found $sdkDir with riscv64-zephyr-elf"
        # Register the SDK in the CMake package registry (idempotent).
        & cmd /c "`"$(Join-Path $sdkDir 'setup.cmd')`" /c"
        Assert-LastExit 'SDK setup.cmd /c'
    } else {
        Push-Location (Join-Path $WorkspaceRoot 'zephyr')
        try {
            # ESP32-C6 (HP and LP core) is RISC-V: only this GNU toolchain is needed.
            & west sdk install -t riscv64-zephyr-elf
            Assert-LastExit 'west sdk install'
        } finally {
            Pop-Location
        }
    }

    # --- 7. Espressif blobs ---------------------------------------------------
    Write-Step 'Espressif binary blobs'
    & west blobs fetch hal_espressif --auto-accept
    Assert-LastExit 'west blobs fetch hal_espressif'
    Write-Ok 'Blobs fetched'
} finally {
    Pop-Location
}

Write-Step 'Done'
Write-Host '    Next: .\scripts\build.ps1, then .\scripts\flash.ps1 -Port COMx and .\scripts\monitor.ps1 -Port COMx'
Show-SerialPorts
