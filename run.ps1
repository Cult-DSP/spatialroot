# run.ps1 — launch the Spatial Root GUI
#
# Must be run from the project root (the directory containing this script).
# The GUI binary receives --root pointing to this directory so that speaker
# layout presets and cult-transcoder resolve correctly.
#
# Usage:
#   .\run.ps1              # launch GUI with project root = repo root
#   .\run.ps1 --help       # pass flags through to the GUI binary

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Visual Studio multi-config generators place binaries under a config subfolder;
# single-config generators (Ninja, NMake) place them directly in the output dir.
$candidates = @(
    "$ScriptDir\build\gui\imgui\Release\Spatial Root.exe",
    "$ScriptDir\build\gui\imgui\Spatial Root.exe"
)

$Binary = $null
foreach ($c in $candidates) {
    if (Test-Path $c) { $Binary = $c; break }
}

if (-not $Binary) {
    Write-Host "Error: `"Spatial Root.exe`" not found. Checked:"
    foreach ($c in $candidates) { Write-Host "  $c" }
    Write-Host ""
    Write-Host "Build the GUI first:"
    Write-Host "  .\build.ps1 --gui"
    exit 1
}

& $Binary --root $ScriptDir @args
