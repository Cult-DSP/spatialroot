# build.ps1 — CMake configure + build for spatialroot (Windows / PowerShell)
#
# Usage:
#   .\build.ps1                  # Build all components (engine, offline, cult)
#   .\build.ps1 -EngineOnly      # Build spatialroot_realtime only
#   .\build.ps1 -OfflineOnly     # Build spatialroot_spatial_render only
#   .\build.ps1 -CultOnly        # Build cult-transcoder only
#   .\build.ps1 -GuiBuild        # Build all + ImGui + GLFW desktop GUI
#
# Run init.ps1 once before the first build to initialize submodules.
# The -GuiBuild flag requires thirdparty/imgui and thirdparty/glfw submodules
# (see init.ps1 — they are initialized automatically when registered).
# Subsequent builds can call build.ps1 directly.

[CmdletBinding()]
param(
    [switch]$EngineOnly,
    [switch]$OfflineOnly,
    [switch]$CultOnly,
    [switch]$GuiBuild,
    [switch]$Help
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ProjectRoot "build"

if ($Help) {
    Write-Host "Usage: .\build.ps1 [--engine-only | --offline-only | --cult-only | --gui-build]"
    exit 0
}

$BuildEngine  = if ($OfflineOnly -or $CultOnly)  { "OFF" } else { "ON" }
$BuildOffline = if ($EngineOnly  -or $CultOnly)   { "OFF" } else { "ON" }
$BuildCult    = if ($EngineOnly  -or $OfflineOnly) { "OFF" } else { "ON" }
$BuildGUI     = if ($GuiBuild) { "ON" } else { "OFF" }

$NumCores = [Environment]::ProcessorCount

Write-Host "============================================================"
Write-Host "spatialroot build (Windows)"
Write-Host "  Engine   (spatialroot_realtime)       : $BuildEngine"
Write-Host "  Offline  (spatialroot_spatial_render) : $BuildOffline"
Write-Host "  CULT     (cult-transcoder)            : $BuildCult"
Write-Host "  GUI      (ImGui + GLFW desktop app)   : $BuildGUI"
Write-Host "  Cores                                 : $NumCores"
Write-Host "============================================================"
Write-Host ""

if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

Write-Host "Configuring CMake..."
cmake `
    -B $BuildDir `
    -DCMAKE_BUILD_TYPE=Release `
    "-DSPATIALROOT_BUILD_ENGINE=$BuildEngine" `
    "-DSPATIALROOT_BUILD_OFFLINE=$BuildOffline" `
    "-DSPATIALROOT_BUILD_CULT=$BuildCult" `
    "-DSPATIALROOT_BUILD_GUI=$BuildGUI" `
    $ProjectRoot

if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ CMake configure failed" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "Building ($NumCores cores)..."
cmake --build $BuildDir --parallel $NumCores --config Release

if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ Build failed" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "============================================================"
Write-Host "✓ Build complete!"
Write-Host ""
if ($BuildEngine -eq "ON") {
    Write-Host "  spatialroot_realtime       : $BuildDir\spatial_engine\realtimeEngine\Release\spatialroot_realtime.exe"
}
if ($BuildOffline -eq "ON") {
    Write-Host "  spatialroot_spatial_render : $BuildDir\spatial_engine\spatialRender\Release\spatialroot_spatial_render.exe"
}
if ($BuildCult -eq "ON") {
    Write-Host "  cult-transcoder            : $BuildDir\cult_transcoder\Release\cult-transcoder.exe"
}
Write-Host "============================================================"
Write-Host ""
