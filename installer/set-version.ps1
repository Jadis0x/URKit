#requires -Version 5.1
<#
    Sets the release version in src/project_version.h and src/app/urk.rc.
      .\installer\set-version.ps1 patch      0.5.0 -> 0.5.1
      .\installer\set-version.ps1 minor      0.5.1 -> 0.6.0
      .\installer\set-version.ps1 major      0.6.0 -> 1.0.0
      .\installer\set-version.ps1 0.7.2      exact version
#>
param([Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$header = Join-Path $RepoRoot 'src\project_version.h'
$resource = Join-Path $RepoRoot 'src\app\urk.rc'

$text = [IO.File]::ReadAllText($header)
if ($text -notmatch 'kReleaseVersion = "(\d+)\.(\d+)\.(\d+)"') { throw "kReleaseVersion not found in $header" }
$parts = [int[]]($Matches[1], $Matches[2], $Matches[3])

switch ($Version) {
    'patch' { $parts[2]++ }
    'minor' { $parts[1]++; $parts[2] = 0 }
    'major' { $parts[0]++; $parts[1] = 0; $parts[2] = 0 }
    default {
        if ($Version -notmatch '^(\d+)\.(\d+)\.(\d+)$') { throw "Version must be patch, minor, major or x.y.z: $Version" }
        $parts = [int[]]($Matches[1], $Matches[2], $Matches[3])
    }
}
$new = $parts -join '.'

function Update-File($Path, $Pattern, $Replacement) {
    $content = [IO.File]::ReadAllText($Path)
    $updated = [regex]::Replace($content, $Pattern, $Replacement)
    if ($updated -eq $content -and $content -notmatch [regex]::Escape($new)) { throw "Nothing to update in $Path" }
    [IO.File]::WriteAllText($Path, $updated)
}

Update-File $header 'kReleaseVersion = "[\d.]+"' "kReleaseVersion = `"$new`""
Update-File $resource 'FILEVERSION [\d,]+' "FILEVERSION $($parts -join ','),0"
Update-File $resource 'PRODUCTVERSION [\d,]+' "PRODUCTVERSION $($parts -join ','),0"
Update-File $resource '"FileVersion", "[\d.]+"' "`"FileVersion`", `"$new`""
Update-File $resource '"ProductVersion", "[\d.]+"' "`"ProductVersion`", `"$new`""

Write-Host "Version set to $new"
