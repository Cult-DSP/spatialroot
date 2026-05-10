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

function Test-SubmoduleRegistered([string]$Path) {
    $gitmodulesPath = Join-Path $ProjectRoot ".gitmodules"
    if (-not (Test-Path $gitmodulesPath)) { return $false }

    $registeredPaths = git config -f $gitmodulesPath --get-regexp '^submodule\..*\.path$' 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $registeredPaths) { return $false }

    foreach ($line in $registeredPaths) {
        $parts = $line -split '\s+', 2
        if ($parts.Count -eq 2 -and $parts[1] -eq $Path) { return $true }
    }
    return $false
}

function Test-SubmoduleMissingRecursive([string]$Path) {
    $status = git submodule status --recursive $Path 2>$null
    foreach ($line in $status) {
        if ($line.StartsWith("-")) { return $true }
    }
    return $false
}

function Ensure-SubmoduleForBuild {
    param(
        [string]$Path,
        [string]$Sentinel,
        [switch]$Recursive
    )

    if ((Test-Path $Sentinel) -and -not (Test-SubmoduleMissingRecursive $Path)) {
        return
    }

    Write-Host "Initializing required submodule: $Path"
    git submodule sync --recursive $Path
    if ($Recursive) {
        git submodule update --init --recursive --depth 1 --checkout $Path
    } else {
        git submodule update --init --depth 1 --checkout $Path
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "✗ Failed to initialize $Path" -ForegroundColor Red
        exit 1
    }
}

if ($Help) {
    Write-Host "Usage: .\build.ps1 [-EngineOnly | -OfflineOnly | -CultOnly | -GuiBuild]"
    exit 0
}

$BuildEngine  = if ($OfflineOnly -or $CultOnly)  { "OFF" } else { "ON" }
$BuildOffline = if ($EngineOnly  -or $CultOnly)   { "OFF" } else { "ON" }
$BuildCult    = if ($EngineOnly  -or $OfflineOnly) { "OFF" } else { "ON" }
$BuildGUI     = if ($GuiBuild) { "ON" } else { "OFF" }
$BuildDevtools = "OFF"

Ensure-SubmoduleForBuild -Path "internal/cult-allolib" -Sentinel (Join-Path $ProjectRoot "internal\cult-allolib\include") -Recursive
Ensure-SubmoduleForBuild -Path "thirdparty/libsndfile" -Sentinel (Join-Path $ProjectRoot "thirdparty\libsndfile\CMakeLists.txt")

if ($BuildCult -eq "ON") {
    Ensure-SubmoduleForBuild -Path "internal/cult_transcoder" -Sentinel (Join-Path $ProjectRoot "internal\cult_transcoder\thirdparty\libbw64\include\bw64\bw64.hpp") -Recursive
}

if ($BuildGUI -eq "ON") {
    if (-not (Test-SubmoduleRegistered "thirdparty/imgui") -or -not (Test-SubmoduleRegistered "thirdparty/glfw")) {
        Write-Host "✗ GUI build requested, but GUI submodules are not fully registered in .gitmodules." -ForegroundColor Red
        Write-Host "  Required: thirdparty/imgui and thirdparty/glfw"
        Write-Host "  Try: .\init.ps1"
        Write-Host "  Or build without -GuiBuild."
        exit 1
    }
    Ensure-SubmoduleForBuild -Path "thirdparty/imgui" -Sentinel (Join-Path $ProjectRoot "thirdparty\imgui\imgui.h")
    Ensure-SubmoduleForBuild -Path "thirdparty/glfw" -Sentinel (Join-Path $ProjectRoot "thirdparty\glfw\CMakeLists.txt")
}

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
    "-DSPATIALROOT_BUILD_DEVTOOLS=$BuildDevtools" `
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
    Write-Host "  spatialroot_realtime       : $BuildDir\source\spatial_engine\realtimeEngine\Release\spatialroot_realtime.exe"
}
if ($BuildOffline -eq "ON") {
    Write-Host "  spatialroot_spatial_render : $BuildDir\source\spatial_engine\spatialRender\Release\spatialroot_spatial_render.exe"
}
if ($BuildCult -eq "ON") {
    Write-Host "  cult-transcoder            : $BuildDir\internal\cult_transcoder\Release\cult-transcoder.exe"
}
if ($BuildGUI -eq "ON") {
    Write-Host "  spatialroot_gui            : $BuildDir\source\gui\imgui\Release\Spatial Root.exe"
}
Write-Host "============================================================"
Write-Host ""
