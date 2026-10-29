#requires -Version 5.1
<#
    Builds the MSVC release and gathers everything to publish into out\release\<version>\:
      URKit-Setup-<version>.exe      installer
      URKit-<version>-portable.zip   urk.exe, the DLLs, licenses and docs in one folder
      SHA256SUMS.txt                 hashes of the two files above
    Run from the repository root:  .\installer\package-release.ps1 [-Clean] [-Bump patch|minor|major|x.y.z]
    -Bump sets the version first (see set-version.ps1); without it the current version is released.
    Needs Inno Setup 6 (winget install JRSoftware.InnoSetup).
#>
param([switch]$Clean, [string]$Bump)
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BinDir = Join-Path $RepoRoot 'out\build\msvc-release'

function Fail($Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

$dirty = & git -C $RepoRoot status --porcelain
if ($dirty) {
    Write-Host "WARNING: uncommitted changes; the release will not match any commit." -ForegroundColor Yellow
}

if ($Bump) { & (Join-Path $PSScriptRoot 'set-version.ps1') $Bump }

$buildArgs = @{ Compiler = 'msvc'; Config = 'Release'; Clean = [bool]$Clean }
& (Join-Path $RepoRoot 'build.ps1') @buildArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $PSScriptRoot 'make-installer.ps1') -SkipBuild
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Same version the installer takes from urk.exe: 0.5.0.0 -> 0.5.0
$info = (Get-Item (Join-Path $BinDir 'urk.exe')).VersionInfo
$version = "$($info.FileMajorPart).$($info.FileMinorPart).$($info.FileBuildPart).$($info.FilePrivatePart)"
if ($version -match '^(.+)\.0$') { $version = $Matches[1] }

$installer = Join-Path $RepoRoot "out\installer\URKit-Setup-$version.exe"
if (-not (Test-Path $installer)) { Fail "Installer not found: $installer" }

$releaseDir = Join-Path $RepoRoot "out\release\$version"
if (Test-Path $releaseDir) { Remove-Item -Recurse -Force $releaseDir }
New-Item -ItemType Directory -Path $releaseDir | Out-Null
Copy-Item $installer $releaseDir

# Portable folder: the same files the installer places, flat, as the README describes.
$stage = Join-Path $releaseDir "URKit-$version"
New-Item -ItemType Directory -Path $stage | Out-Null
foreach ($name in 'urk.exe', 'version.dll', 'winhttp.dll', 'winmm.dll', 'dwmapi.dll', 'URKitInjector.dll', 'URKitDevBridge.dll') {
    Copy-Item (Join-Path $BinDir $name) $stage
}
foreach ($name in 'LICENSE', 'README.md', 'THIRD_PARTY_NOTICES.md') {
    Copy-Item (Join-Path $RepoRoot $name) $stage
}
$zip = Join-Path $releaseDir "URKit-$version-portable.zip"
Compress-Archive -Path $stage -DestinationPath $zip
Remove-Item -Recurse -Force $stage

$sums = Get-ChildItem $releaseDir -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } | ForEach-Object {
    "$((Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower())  $($_.Name)"
}
$sums | Set-Content (Join-Path $releaseDir 'SHA256SUMS.txt') -Encoding ascii

Write-Host ""
Write-Host "== Release $version ready ==" -ForegroundColor Green
Write-Host "Upload these files from: $releaseDir"
Get-ChildItem $releaseDir -File | ForEach-Object { Write-Host "  $($_.Name)" }
$sums | ForEach-Object { Write-Host "  $_" }
