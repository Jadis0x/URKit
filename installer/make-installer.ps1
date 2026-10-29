#requires -Version 5.1
<#
    Builds the MSVC release and packs it into out\installer\URKit-Setup-<version>.exe.
    Run from the repository root:  .\installer\make-installer.ps1
    -SkipBuild packs what is already in out\build\msvc-release (package-release.ps1 uses this).
    Needs Inno Setup 6 (winget install JRSoftware.InnoSetup).
#>
param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BinDir = Join-Path $RepoRoot 'out\build\msvc-release'

$iscc = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    Write-Host "ERROR: Inno Setup 6 not found. Install it with: winget install JRSoftware.InnoSetup" -ForegroundColor Red
    exit 1
}

if (-not $SkipBuild) {
    & (Join-Path $RepoRoot 'build.ps1') -Compiler msvc -Config Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

foreach ($name in 'urk.exe', 'version.dll', 'winhttp.dll', 'winmm.dll', 'dwmapi.dll', 'URKitInjector.dll', 'URKitDevBridge.dll') {
    if (-not (Test-Path (Join-Path $BinDir $name))) {
        Write-Host "ERROR: $name is missing from $BinDir" -ForegroundColor Red
        exit 1
    }
}

& $iscc (Join-Path $PSScriptRoot 'urk.iss') "/DBinDir=$BinDir"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "Installer:" -ForegroundColor Green
Get-ChildItem (Join-Path $RepoRoot 'out\installer') -Filter 'URKit-Setup-*.exe' |
    ForEach-Object { Write-Host "  $($_.FullName)" }
