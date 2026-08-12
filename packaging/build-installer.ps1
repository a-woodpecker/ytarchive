<#
.SYNOPSIS
    Builds YouTube Archive and packages it into a Windows installer.

.DESCRIPTION
    Configures and builds with CMake, runs windeployqt to gather the Qt runtime
    into a staging folder, then compiles the Inno Setup script.

.EXAMPLE
    .\packaging\build-installer.ps1 -QtDir C:\Qt\6.11.1\msvc2022_64
#>

[CmdletBinding()]
param(
    [string]$QtDir      = "C:\Qt\6.11.1\msvc2022_64",
    [string]$Version    = "",
    [string]$Preset     = "windows-vs2026",
    [string]$GitHubRepo = "a-woodpecker/ytarchive",
    [string]$Iscc       = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
)

$ErrorActionPreference = "Stop"
$root    = Split-Path -Parent $PSScriptRoot
$staging = Join-Path $PSScriptRoot "staging"

# Take the version from CMakeLists.txt unless one was passed in, so the
# installer name and the binary's update check can never disagree.
if (-not $Version) {
    $match = Select-String -Path (Join-Path $root "CMakeLists.txt") `
                           -Pattern 'project\(YtArchive VERSION ([0-9.]+)'
    if (-not $match) { throw "Could not read the version from CMakeLists.txt." }
    $Version = $match.Matches[0].Groups[1].Value
}
Write-Host "Building YouTube Archive $Version" -ForegroundColor Cyan

if (-not (Test-Path $QtDir))  { throw "Qt not found at $QtDir. Pass -QtDir." }
if (-not (Test-Path $Iscc))   { throw "Inno Setup not found at $Iscc. Install it, or pass -Iscc." }

# --- configure and build -------------------------------------------------
$cmakeArgs = @("--preset", $Preset, "-DCMAKE_PREFIX_PATH=$QtDir")
if ($GitHubRepo) { $cmakeArgs += "-DYTA_GITHUB_REPO=$GitHubRepo" }

Push-Location $root
try {
    & cmake @cmakeArgs
    if ($LASTEXITCODE) { throw "CMake configure failed." }

    & cmake --build --preset $Preset
    if ($LASTEXITCODE) { throw "Build failed." }
}
finally { Pop-Location }

# Multi-config generators nest the binary one level deeper than Ninja does.
$exe = Get-ChildItem -Path (Join-Path $root "build\$Preset") -Filter "ytarchive.exe" `
                     -Recurse -File | Select-Object -First 1
if (-not $exe) { throw "ytarchive.exe was not produced." }
Write-Host "  built: $($exe.FullName)"

# --- stage the runtime ---------------------------------------------------
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Path $staging | Out-Null

Copy-Item $exe.FullName -Destination $staging
foreach ($doc in @("README.md")) {
    $p = Join-Path $root $doc
    if (Test-Path $p) { Copy-Item $p -Destination $staging }
}

& (Join-Path $QtDir "bin\windeployqt.exe") `
    --release --no-translations --no-opengl-sw --no-system-d3d-compiler `
    (Join-Path $staging "ytarchive.exe")
if ($LASTEXITCODE) { throw "windeployqt failed." }

# windeployqt is driven by the binary's imports; the SQL driver is loaded at
# runtime as a plugin, so verify it rather than assuming it came along.
if (-not (Test-Path (Join-Path $staging "sqldrivers\qsqlite.dll"))) {
    throw "qsqlite.dll is missing from the staging folder. Without it the catalog cannot open."
}
Write-Host "  staged: $staging"

# --- compile the installer ----------------------------------------------
& $Iscc "/DAppVersion=$Version" (Join-Path $PSScriptRoot "ytarchive.iss")
if ($LASTEXITCODE) { throw "Inno Setup failed." }

$out = Join-Path $PSScriptRoot "dist\YouTubeArchive-$Version-setup.exe"
Write-Host "`nInstaller ready:" -ForegroundColor Green
Write-Host "  $out"
Write-Host "`nPublish it as a GitHub release asset tagged v$Version so the"
Write-Host "in-app update check finds it."