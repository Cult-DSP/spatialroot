# init.ps1 — One-time dependency setup for spatialroot (Windows / PowerShell)
#
# Run once after cloning to initialize all git submodules and build all
# C++ components. Subsequent builds can use build.ps1 directly.
#
# Usage:
#   Set-ExecutionPolicy -Scope Process Bypass
#   .\init.ps1
#
# No Python toolchain required.

[CmdletBinding()]
param(
    [switch]$Help
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot

if ($Help) {
    Write-Host "Usage: .\init.ps1"
    Write-Host ""
    Write-Host "Initializes git submodules and builds all C++ components."
    Write-Host "Run once after cloning. Subsequent builds: .\build.ps1"
    exit 0
}

function Section($title) {
    Write-Host "============================================================"
    Write-Host $title
    Write-Host "============================================================"
    Write-Host ""
}

Section "spatialroot Initialization (Windows)"

# ── Step 1: Check build tools ─────────────────────────────────────────────────
Write-Host "Step 1: Checking build tools..."

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "✗ cmake not found. Install CMake 3.20+ and add it to PATH." -ForegroundColor Red
    Write-Host "  https://cmake.org/download/"
    exit 1
}
$cmakeVersion = (cmake --version | Select-Object -First 1).Split(" ")[2]
Write-Host "✓ cmake $cmakeVersion found"

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host "✗ git not found. Install Git for Windows." -ForegroundColor Red
    exit 1
}
Write-Host "✓ git found"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($vsPath) {
        Write-Host "✓ Visual Studio with C++ tools found"
    } else {
        Write-Host "✗ Visual Studio found but C++ workload is missing." -ForegroundColor Red
        Write-Host "  Open Visual Studio Installer and add 'Desktop development with C++'"
        exit 1
    }
} elseif (Get-Command cl -ErrorAction SilentlyContinue) {
    Write-Host "✓ C++ compiler (cl.exe) found"
} else {
    Write-Host "✗ No C++ compiler found." -ForegroundColor Red
    Write-Host "  Install Visual Studio 2019+ with 'Desktop development with C++'"
    Write-Host "  https://visualstudio.microsoft.com/downloads/"
    exit 1
}
Write-Host ""

# ── Step 2: Initialize cult-allolib submodule ────────────────────────────────
Write-Host "Step 2: Initializing cult-allolib submodule..."

$AlloInclude = Join-Path $ProjectRoot "internal\cult-allolib\include"
if (Test-Path $AlloInclude) {
    Write-Host "✓ internal/cult-allolib already initialized"
} else {
    Write-Host "Fetching internal/cult-allolib (shallow, depth=1)..."
    git submodule update --init --recursive --depth 1 --checkout internal/cult-allolib
    if ($LASTEXITCODE -ne 0) {
        Write-Host "✗ Failed to initialize internal/cult-allolib" -ForegroundColor Red
        exit 1
    }
    Write-Host "✓ internal/cult-allolib initialized"
}
Write-Host ""

# ── Step 3: Initialize cult_transcoder submodule ──────────────────────────────
Write-Host "Step 3: Initializing cult_transcoder submodule..."

$CultCMake = Join-Path $ProjectRoot "cult_transcoder\CMakeLists.txt"
if (Test-Path $CultCMake) {
    Write-Host "✓ cult_transcoder already initialized"
} else {
    Write-Host "Fetching cult_transcoder..."
    git submodule update --init --depth 1 --checkout cult_transcoder
    if ($LASTEXITCODE -ne 0) {
        Write-Host "✗ Failed to initialize cult_transcoder" -ForegroundColor Red
        exit 1
    }
    Write-Host "✓ cult_transcoder initialized"
}

# cult_transcoder owns nested vendored deps that must be present before configure.
$Libbw64Header = Join-Path $ProjectRoot "cult_transcoder\thirdparty\libbw64\include\bw64\bw64.hpp"
$R8brainHeader = Join-Path $ProjectRoot "cult_transcoder\thirdparty\r8brain\CDSPResampler.h"
if ((Test-Path $Libbw64Header) -and (Test-Path $R8brainHeader)) {
    Write-Host "✓ cult_transcoder nested submodules already initialized"
} else {
    Write-Host "Fetching cult_transcoder nested submodules recursively..."
    Push-Location (Join-Path $ProjectRoot "cult_transcoder")
    git submodule update --init --recursive --depth 1 --checkout
    $exitCode = $LASTEXITCODE
    Pop-Location
    if ($exitCode -ne 0) {
        Write-Host "✗ Failed to initialize cult_transcoder nested submodules" -ForegroundColor Red
        exit 1
    }
    Write-Host "✓ cult_transcoder nested submodules initialized"
}
Write-Host ""

# ── Step 4: Initialize libsndfile submodule ──────────────────────────────────
Write-Host "Step 4: Initializing libsndfile submodule..."

$LibSndFileCMake = Join-Path $ProjectRoot "thirdparty\libsndfile\CMakeLists.txt"
if (Test-Path $LibSndFileCMake) {
    Write-Host "✓ thirdparty/libsndfile already initialized"
} else {
    Write-Host "Fetching thirdparty/libsndfile..."
    git submodule update --init --depth 1 --checkout thirdparty/libsndfile
    if ($LASTEXITCODE -ne 0) {
        Write-Host "✗ Failed to initialize thirdparty/libsndfile" -ForegroundColor Red
        exit 1
    }
    Write-Host "✓ thirdparty/libsndfile initialized"
}
Write-Host ""

# ── Step 5: Initialize Dear ImGui submodule (optional — needed for GUI build) ─
Write-Host "Step 5: Initializing Dear ImGui submodule..."

$GitmodulesPath = Join-Path $ProjectRoot ".gitmodules"
$ImGuiDir = Join-Path $ProjectRoot "thirdparty\imgui"
if ((Test-Path $GitmodulesPath) -and (Select-String -Path $GitmodulesPath -Pattern "thirdparty/imgui" -Quiet)) {
    $ImGuiHeader = Join-Path $ImGuiDir "imgui.h"
    if (Test-Path $ImGuiHeader) {
        Write-Host "✓ thirdparty/imgui already initialized"
    } else {
        Write-Host "Fetching thirdparty/imgui..."
        git submodule update --init --depth 1 --checkout thirdparty/imgui
        if ($LASTEXITCODE -ne 0) {
            Write-Host "✗ Failed to initialize thirdparty/imgui" -ForegroundColor Red
            exit 1
        }
        Write-Host "✓ thirdparty/imgui initialized"
    }
} else {
    Write-Host "ℹ  thirdparty/imgui not registered (GUI build not enabled)"
}
Write-Host ""

# ── Step 6: Initialize GLFW submodule (optional — needed for GUI build) ──────
Write-Host "Step 6: Initializing GLFW submodule..."

$GlfwDir = Join-Path $ProjectRoot "thirdparty\glfw"
if ((Test-Path $GitmodulesPath) -and (Select-String -Path $GitmodulesPath -Pattern "thirdparty/glfw" -Quiet)) {
    $GlfwCMake = Join-Path $GlfwDir "CMakeLists.txt"
    if (Test-Path $GlfwCMake) {
        Write-Host "✓ thirdparty/glfw already initialized"
    } else {
        Write-Host "Fetching thirdparty/glfw..."
        git submodule update --init --depth 1 --checkout thirdparty/glfw
        if ($LASTEXITCODE -ne 0) {
            Write-Host "✗ Failed to initialize thirdparty/glfw" -ForegroundColor Red
            exit 1
        }
        Write-Host "✓ thirdparty/glfw initialized"
    }
} else {
    Write-Host "ℹ  thirdparty/glfw not registered (GUI build not enabled)"
}
Write-Host ""

# ── Step 7: Build all C++ components ─────────────────────────────────────────
Write-Host "Step 7: Building all C++ components..."
Write-Host ""

$buildScript = Join-Path $ProjectRoot "build.ps1"
& $buildScript -GuiBuild

if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ Build failed. Check CMake output above." -ForegroundColor Red
    exit 1
}

Write-Host ""
Section "✓ Initialization complete!"

Write-Host "Binaries:"
Write-Host "  spatialroot_realtime       : build\spatial_engine\realtimeEngine\Release\spatialroot_realtime.exe"
Write-Host "  spatialroot_spatial_render : build\spatial_engine\spatialRender\Release\spatialroot_spatial_render.exe"
Write-Host "  cult-transcoder            : build\cult_transcoder\Release\cult-transcoder.exe"
Write-Host ""
Write-Host "For subsequent full builds:"
Write-Host "  .\build.ps1"
Write-Host ""
