# Sync MetahumanGizmoRuntime from this repo to a UE project plugin copy.
# Default target: F:\UEProjects\MetaHumans57\Plugins\MetahumanGizmoRuntime
# Usage: pwsh -File Scripts\Sync-To-MetaHumans57.ps1
# Robocopy exit 0–7 = success (see robocopy /?).

$ErrorActionPreference = 'Stop'
# Scripts\ -> plugin root
$Here = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $Here 'MetahumanGizmoRuntime.uplugin'))) {
    $Here = $PSScriptRoot
    while ($Here -and -not (Test-Path (Join-Path $Here 'MetahumanGizmoRuntime.uplugin'))) {
        $Here = Split-Path -Parent $Here
    }
}
if (-not $Here -or -not (Test-Path (Join-Path $Here 'MetahumanGizmoRuntime.uplugin'))) {
    Write-Error 'Could not locate plugin root (MetahumanGizmoRuntime.uplugin). Run from repo or set $SourceRoot.'
    exit 2
}

$SourceRoot = $Here
$DestRoot = if ($env:METAHUMAN_GIZMO_SYNC_DEST) { $env:METAHUMAN_GIZMO_SYNC_DEST } else { 'F:\UEProjects\MetaHumans57\Plugins\MetahumanGizmoRuntime' }

if (-not (Test-Path $DestRoot)) {
    Write-Error "Destination does not exist: $DestRoot — create the folder or set METAHUMAN_GIZMO_SYNC_DEST."
    exit 3
}

Write-Host "Source: $SourceRoot"
Write-Host "Dest:   $DestRoot"

$robocopyArgs = @(
    $SourceRoot, $DestRoot, '/E',
    '/XD', 'Binaries', 'Intermediate', 'Saved', '.git', '.vs',
    '/NFL', '/NDL', '/NJH', '/NJS', '/nc', '/ns', '/np'
)
& robocopy @robocopyArgs
$code = $LASTEXITCODE
if ($code -ge 8) {
    Write-Error "robocopy failed with exit code $code"
    exit $code
}
Write-Host "Done (robocopy code $code; 0–7 = OK)."
exit 0
