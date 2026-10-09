param(
    [switch]$Release,
    [switch]$Debug,
    [switch]$NoBuild,
    [switch]$BuildOnly,
    [switch]$Configure,
    [ValidateRange(1, 256)]
    [int]$Jobs,
    [string[]]$AppArguments
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDir ".."))
$isDebugBuild = -not $Release
$configurePreset = if ($isDebugBuild) { "dev-windows-debug" } else { "release-windows" }
$buildPreset = "build-$configurePreset"
$buildDir = Join-Path $repoRoot "build\$configurePreset"
$appBinary = Join-Path $buildDir "apps\desktop\Looksy.exe"
$rustProfile = if ($isDebugBuild) { "debug" } else { "release" }
$rustBinDir = Join-Path $repoRoot "rust\target\$rustProfile"
$script:vsInstallPath = $null

function Write-Step([string]$Message) {
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Write-Info([string]$Message) {
    Write-Host "    $Message" -ForegroundColor DarkGray
}

function Fail([string]$Message) {
    throw $Message
}

function Find-CommandPath([string]$Name) {
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -eq $command) {
        return $null
    }
    return $command.Source
}

function Require-Command([string]$Name, [string]$InstallHint) {
    $path = Find-CommandPath $Name
    if (-not $path) {
        Fail "$Name is required. $InstallHint"
    }
    return $path
}

function Resolve-CMake([string]$PreVisualStudioPath) {
    $minimumVersion = [version]"3.28.0"
    $candidates = New-Object System.Collections.Generic.List[string]

    if ($env:PHOTO_EDITOR_CMAKE_EXE) {
        [void]$candidates.Add($env:PHOTO_EDITOR_CMAKE_EXE)
    }
    [void]$candidates.Add("C:\Program Files\CMake\bin\cmake.exe")
    if ($PreVisualStudioPath) {
        [void]$candidates.Add($PreVisualStudioPath)
    }
    foreach ($command in @(Get-Command "cmake.exe" -CommandType Application -All -ErrorAction SilentlyContinue)) {
        [void]$candidates.Add($command.Source)
    }

    $seen = @{}
    $found = New-Object System.Collections.Generic.List[string]
    foreach ($candidate in $candidates) {
        if (-not $candidate) {
            continue
        }
        $resolved = [System.IO.Path]::GetFullPath($candidate)
        if ($seen.ContainsKey($resolved) -or -not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            continue
        }
        $seen[$resolved] = $true

        $versionOutput = & $resolved --version 2>$null
        if ($LASTEXITCODE -ne 0) {
            continue
        }
        $versionLine = $versionOutput | Select-Object -First 1
        if ($versionLine -notmatch '^cmake version ([0-9]+(?:\.[0-9]+){1,3})') {
            continue
        }
        $version = [version]$Matches[1]
        [void]$found.Add("$resolved ($version)")
        if ($version -ge $minimumVersion) {
            return $resolved
        }
    }

    $details = if ($found.Count -gt 0) { " Found: $($found -join '; ')." } else { "" }
    Fail "CMake 3.28 or newer was not found.$details Install it with 'winget install --exact --id Kitware.CMake', reopen PowerShell, and rerun this script. Visual Studio 2019's bundled CMake is too old for this project's presets."
}

function Resolve-Python {
    $minimumVersion = [version]"3.7.0"
    $candidates = New-Object System.Collections.Generic.List[string]

    if ($env:PHOTO_EDITOR_PYTHON_EXE) {
        [void]$candidates.Add($env:PHOTO_EDITOR_PYTHON_EXE)
    }

    $pyLauncher = Find-CommandPath "py.exe"
    if ($pyLauncher) {
        $launchedPython = & $pyLauncher -3 -c "import sys; print(sys.executable)" 2>$null
        if ($LASTEXITCODE -eq 0 -and $launchedPython) {
            [void]$candidates.Add(([string]($launchedPython | Select-Object -First 1)).Trim())
        }
    }

    $pythonBase = Join-Path $env:LOCALAPPDATA "Programs\Python"
    if (Test-Path -LiteralPath $pythonBase -PathType Container) {
        foreach ($installation in @(Get-ChildItem -LiteralPath $pythonBase -Directory | Sort-Object Name -Descending)) {
            [void]$candidates.Add((Join-Path $installation.FullName "python.exe"))
        }
    }
    foreach ($command in @(Get-Command "python.exe" -CommandType Application -All -ErrorAction SilentlyContinue)) {
        [void]$candidates.Add($command.Source)
    }

    $seen = @{}
    foreach ($candidate in $candidates) {
        if (-not $candidate) {
            continue
        }
        $resolved = [System.IO.Path]::GetFullPath($candidate)
        if ($seen.ContainsKey($resolved) -or -not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            continue
        }
        $seen[$resolved] = $true

        $versionOutput = & $resolved -c "import sys; print('.'.join(map(str, sys.version_info[:3])))" 2>$null
        if ($LASTEXITCODE -ne 0 -or -not $versionOutput) {
            continue
        }
        $version = [version](([string]($versionOutput | Select-Object -First 1)).Trim())
        if ($version -ge $minimumVersion) {
            return $resolved
        }
    }

    Fail "Python 3.7 or newer was not found. Install it with 'winget install --exact --id Python.Python.3.12', reopen PowerShell, and rerun this script. The zero-byte C:\Windows\System32\python launcher is not a usable Python installation."
}

function Invoke-Native([string]$FilePath, [string[]]$Arguments) {
    $preview = @($FilePath) + $Arguments
    Write-Info ("$ " + ($preview -join " "))
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        Fail "Command failed with exit code $LASTEXITCODE`: $FilePath"
    }
}

function Import-VisualStudioEnvironment {
    if (Find-CommandPath "cl.exe") {
        return
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        Fail "MSVC was not found. Install Visual Studio 2019 or newer Build Tools with the 'Desktop development with C++' workload."
    }

    $installation = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) {
        Fail "Visual Studio is installed without the Desktop development with C++ workload."
    }

    $script:vsInstallPath = ([string]$installation).Trim()
    $devCmd = Join-Path $script:vsInstallPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $devCmd -PathType Leaf)) {
        Fail "Visual Studio developer environment script was not found: $devCmd"
    }

    Write-Info "Loading MSVC x64 environment from $script:vsInstallPath"
    $commandLine = "`"$devCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
    $environmentLines = & $env:COMSPEC /s /c $commandLine
    if ($LASTEXITCODE -ne 0) {
        Fail "Visual Studio's x64 developer environment could not be loaded."
    }
    foreach ($line in $environmentLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2]
        }
    }

    if (-not (Find-CommandPath "cl.exe")) {
        Fail "MSVC was not available after loading the Visual Studio developer environment."
    }
}

function Resolve-QtRoot {
    $deployCommand = Find-CommandPath "windeployqt.exe"
    if ($deployCommand) {
        return Split-Path -Parent (Split-Path -Parent $deployCommand)
    }

    $candidates = New-Object System.Collections.Generic.List[string]
    foreach ($value in @($env:QT_ROOT, $env:QTDIR)) {
        if ($value) {
            [void]$candidates.Add($value)
        }
    }

    $qtBase = "C:\Qt"
    if (Test-Path -LiteralPath $qtBase -PathType Container) {
        $versions = Get-ChildItem -LiteralPath $qtBase -Directory |
            Sort-Object -Property Name -Descending
        foreach ($version in $versions) {
            $kits = Get-ChildItem -LiteralPath $version.FullName -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -like "msvc*_64" } |
                Sort-Object -Property Name -Descending
            foreach ($kit in $kits) {
                [void]$candidates.Add($kit.FullName)
            }
        }
    }

    foreach ($candidate in $candidates) {
        $resolved = [System.IO.Path]::GetFullPath($candidate)
        if ((Test-Path -LiteralPath (Join-Path $resolved "bin\windeployqt.exe") -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $resolved "lib\cmake\Qt6\Qt6Config.cmake") -PathType Leaf)) {
            return $resolved
        }
    }

    Fail "Qt was not found. Install Qt 6.7.3 for MSVC 2019 64-bit with Qt Multimedia, Shader Tools, Qt 5 Compatibility, and Qt Image Formats; then set QT_ROOT to that kit (for example C:\Qt\6.7.3\msvc2019_64)."
}

function Resolve-VcpkgRoot {
    $candidates = New-Object System.Collections.Generic.List[string]
    foreach ($value in @($env:VCPKG_ROOT, $env:VCPKG_INSTALLATION_ROOT)) {
        if ($value) {
            [void]$candidates.Add($value)
        }
    }
    if ($script:vsInstallPath) {
        [void]$candidates.Add((Join-Path $script:vsInstallPath "VC\vcpkg"))
    }
    [void]$candidates.Add("C:\vcpkg")

    $vcpkgCommand = Find-CommandPath "vcpkg.exe"
    if ($vcpkgCommand) {
        [void]$candidates.Insert(0, (Split-Path -Parent $vcpkgCommand))
    }

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate "scripts\buildsystems\vcpkg.cmake") -PathType Leaf)) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    Fail "vcpkg was not found. Set VCPKG_ROOT to a vcpkg checkout, or install the Visual Studio vcpkg component."
}

function Resolve-PostgresBin {
    if ($env:PHOTO_EDITOR_POSTGRES_BIN_DIR) {
        $configured = [System.IO.Path]::GetFullPath($env:PHOTO_EDITOR_POSTGRES_BIN_DIR)
        if (Test-Path -LiteralPath (Join-Path $configured "pg_ctl.exe") -PathType Leaf) {
            return $configured
        }
        Fail "PHOTO_EDITOR_POSTGRES_BIN_DIR does not contain pg_ctl.exe: $configured"
    }

    $pgCtl = Find-CommandPath "pg_ctl.exe"
    if ($pgCtl) {
        return Split-Path -Parent $pgCtl
    }

    $postgresBase = Join-Path $env:ProgramFiles "PostgreSQL"
    if (Test-Path -LiteralPath $postgresBase -PathType Container) {
        $installations = Get-ChildItem -LiteralPath $postgresBase -Directory |
            Sort-Object -Property Name -Descending
        foreach ($installation in $installations) {
            $binDir = Join-Path $installation.FullName "bin"
            if (Test-Path -LiteralPath (Join-Path $binDir "pg_ctl.exe") -PathType Leaf) {
                return $binDir
            }
        }
    }

    Fail "PostgreSQL was not found. Install PostgreSQL 16, add its bin directory to PATH, or set PHOTO_EDITOR_POSTGRES_BIN_DIR. You may instead set PHOTO_EDITOR_DATABASE_URL to an existing PostgreSQL database."
}

function Stop-ExistingApp([string]$ExpectedPath) {
    $normalizedExpected = [System.IO.Path]::GetFullPath($ExpectedPath)
    $matching = @()
    foreach ($process in @(Get-Process -Name "Looksy" -ErrorAction SilentlyContinue)) {
        try {
            if ($process.Path -and ([System.IO.Path]::GetFullPath($process.Path) -ieq $normalizedExpected)) {
                $matching += $process
            }
        } catch {
            Write-Info "Could not inspect Looksy process $($process.Id); leaving it untouched."
        }
    }

    foreach ($process in $matching) {
        Write-Host "Stopping existing Looksy instance ($($process.Id))..." -ForegroundColor Yellow
        if (-not $process.CloseMainWindow()) {
            Fail "Looksy process $($process.Id) did not accept a normal close request. Close it manually and retry."
        }
        if (-not $process.WaitForExit(10000)) {
            Fail "Looksy process $($process.Id) has not exited. Close it manually and retry."
        }
    }
}

if ($Release -and $Debug) {
    Fail "Choose either -Release or -Debug, not both."
}
if ($NoBuild -and ($BuildOnly -or $Configure -or $PSBoundParameters.ContainsKey("Jobs"))) {
    Fail "-NoBuild cannot be combined with -BuildOnly, -Configure, or -Jobs."
}

Write-Host "`n============================================================" -ForegroundColor Cyan
Write-Host "  Looksy native Windows run" -ForegroundColor Cyan
Write-Host "  preset: $configurePreset  build: $buildPreset" -ForegroundColor DarkCyan
Write-Host "============================================================" -ForegroundColor Cyan

if (-not $NoBuild) {
    Write-Step "Windows build toolchain"
    $cmakeBeforeVisualStudio = Find-CommandPath "cmake.exe"
    Import-VisualStudioEnvironment
    $cmake = Resolve-CMake $cmakeBeforeVisualStudio
    $ninja = Require-Command "ninja.exe" "Install Ninja and add it to PATH."
    $python = Resolve-Python
    $cargo = Require-Command "cargo.exe" "Install the stable Rust toolchain from https://rustup.rs/."
    $qtRoot = Resolve-QtRoot
    $vcpkgRoot = Resolve-VcpkgRoot
    $toolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    $windeployqt = Join-Path $qtRoot "bin\windeployqt.exe"
    $pythonForCMake = $python -replace '\\', '/'

    $qtBin = Join-Path $qtRoot "bin"
    if (($env:Path -split ';') -notcontains $qtBin) {
        $env:Path = "$qtBin;$env:Path"
    }
    $pythonBin = Split-Path -Parent $python
    if (($env:Path -split ';') -notcontains $pythonBin) {
        $env:Path = "$pythonBin;$env:Path"
    }
    if ($env:CMAKE_PREFIX_PATH) {
        $env:CMAKE_PREFIX_PATH = "$qtRoot;$env:CMAKE_PREFIX_PATH"
    } else {
        $env:CMAKE_PREFIX_PATH = $qtRoot
    }

    Write-Info "MSVC: $((Find-CommandPath 'cl.exe'))"
    Write-Info "CMake: $cmake"
    Write-Info "Ninja: $ninja"
    Write-Info "Python: $python"
    Write-Info "Qt: $qtRoot"
    Write-Info "vcpkg: $vcpkgRoot"

    Write-Step "Rust catalog binaries"
    $cargoArguments = @("build", "--manifest-path", (Join-Path $repoRoot "rust\Cargo.toml"), "-p", "photo-catalog", "--bins")
    if (-not $isDebugBuild) {
        $cargoArguments += "--release"
    }
    if ($PSBoundParameters.ContainsKey("Jobs")) {
        $cargoArguments += @("--jobs", [string]$Jobs)
    }
    Invoke-Native $cargo $cargoArguments

    $cacheFile = Join-Path $buildDir "CMakeCache.txt"
    $buildSystemFile = Join-Path $buildDir "build.ninja"
    $needsConfigure = ($Configure -or
        -not (Test-Path -LiteralPath $cacheFile -PathType Leaf) -or
        -not (Test-Path -LiteralPath $buildSystemFile -PathType Leaf))
    if ($needsConfigure) {
        Write-Step "Windows desktop configure"
        $configureArguments = @(
            "--preset", $configurePreset,
            "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
            "-DCMAKE_MAKE_PROGRAM=$ninja",
            "-DVCPKG_TARGET_TRIPLET=x64-windows",
            "-DVCPKG_INSTALL_OPTIONS=--cmake-args=-DPYTHON3=$pythonForCMake",
            "-DCMAKE_PREFIX_PATH=$qtRoot",
            "-DPHOTO_EDITOR_RUST_BINARY_DIR=$rustBinDir"
        )
        Push-Location $repoRoot
        try {
            Invoke-Native $cmake $configureArguments
        } finally {
            Pop-Location
        }
    }

    Write-Step "Windows desktop build"
    $buildArguments = @("--build", "--preset", $buildPreset, "--target", "photo_editor_app")
    if ($PSBoundParameters.ContainsKey("Jobs")) {
        $buildArguments += @("--parallel", [string]$Jobs)
    }
    Push-Location $repoRoot
    try {
        Invoke-Native $cmake $buildArguments
    } finally {
        Pop-Location
    }

    if (-not (Test-Path -LiteralPath $appBinary -PathType Leaf)) {
        Fail "Build completed without producing the app: $appBinary"
    }

    Write-Step "Qt runtime deployment"
    $deployArguments = @(
        "--qmldir", (Join-Path $repoRoot "apps\desktop\qml"),
        "--no-translations",
        $(if ($isDebugBuild) { "--debug" } else { "--release" }),
        $appBinary
    )
    Invoke-Native $windeployqt $deployArguments
}

if (-not (Test-Path -LiteralPath $appBinary -PathType Leaf)) {
    Fail "App not built: $appBinary. Run this script without -NoBuild first."
}
if ($BuildOnly) {
    Write-Host "`nApp ready: $appBinary" -ForegroundColor Green
    exit 0
}

if (-not $env:PHOTO_EDITOR_APP_DATA_DIR) {
    $env:PHOTO_EDITOR_APP_DATA_DIR = Join-Path $env:APPDATA "PhotoEditor"
}
if (-not $env:PHOTO_EDITOR_LOG_DIR) {
    $env:PHOTO_EDITOR_LOG_DIR = Join-Path $env:PHOTO_EDITOR_APP_DATA_DIR "logs"
}
if (-not $env:PHOTO_EDITOR_BACKUP_DIR) {
    $env:PHOTO_EDITOR_BACKUP_DIR = Join-Path $env:PHOTO_EDITOR_APP_DATA_DIR "backups"
}
[void](New-Item -ItemType Directory -Force -Path $env:PHOTO_EDITOR_APP_DATA_DIR)
[void](New-Item -ItemType Directory -Force -Path $env:PHOTO_EDITOR_LOG_DIR)
[void](New-Item -ItemType Directory -Force -Path $env:PHOTO_EDITOR_BACKUP_DIR)

Write-Step "Catalog database"
if ($env:PHOTO_EDITOR_DATABASE_URL) {
    $catalogHelper = Join-Path $rustBinDir "catalog_migrate.exe"
    if (-not (Test-Path -LiteralPath $catalogHelper -PathType Leaf)) {
        Fail "Catalog helper not built: $catalogHelper"
    }
    Invoke-Native $catalogHelper @()
} else {
    $env:PHOTO_EDITOR_POSTGRES_BIN_DIR = Resolve-PostgresBin
    Write-Info "PostgreSQL: $env:PHOTO_EDITOR_POSTGRES_BIN_DIR"
    $catalogHelper = Join-Path $rustBinDir "catalog_local_db.exe"
    if (-not (Test-Path -LiteralPath $catalogHelper -PathType Leaf)) {
        Fail "Catalog helper not built: $catalogHelper"
    }
    Invoke-Native $catalogHelper @("ensure-running")
}

Stop-ExistingApp $appBinary
Write-Host "`nLaunching: $appBinary" -ForegroundColor Magenta
$startParameters = @{
    FilePath = $appBinary
    PassThru = $true
}
if ($AppArguments -and $AppArguments.Count -gt 0) {
    $startParameters.ArgumentList = $AppArguments
}
$process = Start-Process @startParameters
$process.WaitForExit()
exit $process.ExitCode
