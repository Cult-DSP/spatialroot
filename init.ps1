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

function Ensure-Submodule {
    param(
        [string]$Path,
        [string]$Sentinel,
        [switch]$Recursive
    )

    if ((Test-Path $Sentinel) -and -not (Test-SubmoduleMissingRecursive $Path)) {
        Write-Host "✓ $Path already initialized"
        return
    }

    Write-Host "Fetching $Path..."
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
    Write-Host "✓ $Path initialized"
}

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

# ── Step 2: Initialize LUSID submodule ───────────────────────────────────────
Write-Host "Step 2: Initializing LUSID submodule..."
Ensure-Submodule -Path "internal/LUSID" -Sentinel (Join-Path $ProjectRoot "internal\LUSID\README.md")
Write-Host ""

# ── Step 3: Initialize cult-allolib submodule ────────────────────────────────
Write-Host "Step 3: Initializing cult-allolib submodule..."
Ensure-Submodule -Path "internal/cult-allolib" -Sentinel (Join-Path $ProjectRoot "internal\cult-allolib\include") -Recursive
Write-Host ""

# ── Step 4: Initialize cult_transcoder submodule ─────────────────────────────
Write-Host "Step 4: Initializing cult_transcoder submodule..."
Ensure-Submodule -Path "internal/cult_transcoder" -Sentinel (Join-Path $ProjectRoot "internal\cult_transcoder\thirdparty\libbw64\include\bw64\bw64.hpp") -Recursive
Write-Host ""

# ── Step 5: Initialize libsndfile submodule ──────────────────────────────────
Write-Host "Step 5: Initializing libsndfile submodule..."
Ensure-Submodule -Path "thirdparty/libsndfile" -Sentinel (Join-Path $ProjectRoot "thirdparty\libsndfile\CMakeLists.txt")
Write-Host ""

# ── Step 6: Initialize Dear ImGui submodule (optional — needed for GUI build) ─
Write-Host "Step 6: Initializing Dear ImGui submodule..."

$ImGuiDir = Join-Path $ProjectRoot "thirdparty\imgui"
$GuiImgSubmodule = $false
$GuiGlfwSubmodule = $false
if (Test-SubmoduleRegistered "thirdparty/imgui") {
    $GuiImgSubmodule = $true
    Ensure-Submodule -Path "thirdparty/imgui" -Sentinel (Join-Path $ImGuiDir "imgui.h")
} else {
    Write-Host "ℹ  thirdparty/imgui not registered"
}
Write-Host ""

# ── Step 7: Initialize GLFW submodule (optional — needed for GUI build) ──────
Write-Host "Step 7: Initializing GLFW submodule..."

$GlfwDir = Join-Path $ProjectRoot "thirdparty\glfw"
if (Test-SubmoduleRegistered "thirdparty/glfw") {
    $GuiGlfwSubmodule = $true
    Ensure-Submodule -Path "thirdparty/glfw" -Sentinel (Join-Path $GlfwDir "CMakeLists.txt")
} else {
    Write-Host "ℹ  thirdparty/glfw not registered"
}
Write-Host ""

# ── Step 8: Build all C++ components ─────────────────────────────────────────
Write-Host "Step 8: Building all C++ components..."
Write-Host ""

$buildScript = Join-Path $ProjectRoot "build.ps1"
$GuiBuildEnabled = $false
if ($GuiImgSubmodule -and $GuiGlfwSubmodule) {
    $GuiBuildEnabled = $true
    Write-Host "GUI submodules detected — building GUI target too."
    Write-Host ""
    & $buildScript -GuiBuild
} else {
    Write-Host "GUI submodules are not fully registered — building non-GUI targets only."
    Write-Host "Register both thirdparty/imgui and thirdparty/glfw in .gitmodules to enable GUI builds."
    Write-Host ""
    & $buildScript
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ Build failed. Check CMake output above." -ForegroundColor Red
    exit 1
}

Write-Host ""
Section "✓ Initialization complete!"

Write-Host "Binaries:"
Write-Host "  spatialroot_realtime       : build\source\spatial_engine\realtimeEngine\Release\spatialroot_realtime.exe"
Write-Host "  spatialroot_spatial_render : build\source\spatial_engine\spatialRender\Release\spatialroot_spatial_render.exe"
Write-Host "  cult-transcoder            : build\internal\cult_transcoder\Release\cult-transcoder.exe"
if ($GuiBuildEnabled) {
    Write-Host "  spatialroot_gui            : build\source\gui\imgui\Release\Spatial Root.exe"
}
Write-Host ""
Write-Host "For subsequent full builds:"
if ($GuiBuildEnabled) {
    Write-Host "  .\build.ps1 -GuiBuild"
} else {
    Write-Host "  .\build.ps1"
}
Write-Host ""
