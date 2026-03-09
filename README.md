Cult DSP — Open Spatial Audio Infrastructure  
Lead Developer: Lucian Parisi

# spatialroot

This repository contains a comprehensive **spatial audio infrastructure** for decoding Audio Definition Model Broadcast WAV (ADM BWF) files — Atmos masters — with mapping to speaker arrays using multiple spatializers (DBAP, VBAP, LBAP). The system includes both real-time performance capabilities and modern offline batch rendering pipeline.

## Quick Start

### First Time Setup

Run this **once** to set up everything:

**macOS/Linux:**

```bash
git clone https://github.com/Cult-DSP/spatialroot.git
cd spatialroot
source init.sh
```

**Windows (PowerShell):**

```powershell
git clone https://github.com/Cult-DSP/spatialroot.git
cd spatialroot
.\init.ps1
```

**Important:** Use `source init.sh` (not `./init.sh`) on macOS/Linux to ensure the virtual environment activates in your current shell. On Windows, the PowerShell/Command Prompt scripts handle activation automatically.

**If you need to reactivate the virtual environment in a new PowerShell session:**

```powershell
cd spatialroot
. .\spatialroot\bin\Activate.ps1
```

You'll know the virtual environment is active when you see `(spatialroot)` in your PowerShell prompt.

The setup scripts will:

-- Create a Python virtual environment (`spatialroot/`)

- Install all Python dependencies
- Initialize git submodules (AlloLib, libbw64, libadm)
  -- Build the embedded ADM extractor (`spatialroot_adm_extract[.exe]`)
- Build the Spatial renderer (supports DBAP, VBAP, LBAP)
- Activate the virtual environment automatically

After setup completes, you'll see `(spatialroot)` in your terminal prompt.

### Get Example Files

```bash
python utils/getExamples.py
```

This downloads example Atmos ADM files for testing.

### Run the Realtime Engine

For live spatial audio playback:

```bash
# With LUSID scene and mono stems
python realtimeMain.py --scene processedData/stageForRender/scene.lusid.json --sources processedData/stageForRender/ --layout spatial_engine/speaker_layouts/allosphere_layout.json

# With ADM file (direct streaming, no stem splitting)
python realtimeMain.py --adm sourceData/driveExampleSpruce.wav --scene processedData/scene.lusid.json --layout spatial_engine/speaker_layouts/allosphere_layout.json

# With GUI
python realtimeMain.py --scene processedData/stageForRender/scene.lusid.json --sources processedData/stageForRender/ --layout spatial_engine/speaker_layouts/allosphere_layout.json --gui
```

**Realtime options:**

- `--scene` - LUSID scene JSON file
- `--sources` - Directory with mono WAV stems (for LUSID packages)
- `--adm` - Multichannel ADM WAV file (direct streaming)
- `--layout` - Speaker layout JSON
- `--gui` - Launch realtime GUI for parameter control
- `--osc_port` - OSC port for external control (default: 12345)

See [`internalDocsMD/Realtime_Engine/realtimeEngine_designDoc.md`](internalDocsMD/Realtime_Engine/realtimeEngine_designDoc.md) for full documentation.

---

## Usage in other projects

See internalDocsMD/AGENTS.md for help implementing in other projects.

## Realtime Spatial Audio

The realtime engine performs live spatial audio rendering using the same LUSID scenes and spatializers as the offline pipeline. It streams audio through your system's audio device for real-time playback.

**Key Features:**

- **Live Playback**: Real-time spatial audio with OSC control
- **Multiple Input Modes**: LUSID packages (mono stems) or direct ADM streaming
- **Spatializers**: DBAP, VBAP, LBAP with configurable parameters
- **OSC Integration**: External control via Open Sound Control
- **GUI Control**: Optional PySide6 interface for parameter adjustment

**Architecture:**

The engine uses a sequential agent model with double-buffered streaming, pose interpolation, and DBAP spatialization. All agents share thread-safe configuration via atomics.

**Supported Spatializers:**

| Feature          | DBAP (default)            | VBAP                  | LBAP                 |
| ---------------- | ------------------------- | --------------------- | -------------------- |
| **Coverage**     | No gaps (works anywhere)  | Can have gaps         | No gaps              |
| **Layout Req**   | Any layout                | Good 3D triangulation | Multi-ring layers    |
| **Localization** | Moderate                  | Precise               | Moderate             |
| **Best For**     | Unknown/irregular layouts | Dense 3D arrays       | Allosphere, TransLAB |

See [`internalDocsMD/Realtime_Engine/realtimeEngine_designDoc.md`](internalDocsMD/Realtime_Engine/realtimeEngine_designDoc.md) for detailed architecture documentation.

---

## Opening a New Terminal Session

**IMPORTANT:** If you close your terminal and come back later, you need to reactivate the virtual environment:

```bash
cd spatialroot
source activate.sh
```

You'll know the virtual environment is active when you see `(spatialroot)` at the start of your terminal prompt.

**Why?** Virtual environments only last for your current terminal session. This is standard Python practice and keeps your system Python clean and isolated from project dependencies.

---

## Troubleshooting

### "ModuleNotFoundError" or "command not found: python"

**Problem:** The virtual environment is not active.

**Solution:** Run this in your terminal:

```bash
source activate.sh
```

Check that you see `(spatialroot)` in your prompt. If you don't see it, the venv is not active.

### Dependency or build errors

If you encounter dependency errors:

```bash
rm .init_complete
source init.sh
```

### Rebuilding the Renderers

After making changes to C++ source files (`spatial_engine/src/` or `spatial_engine/realtimeEngine/src/`), rebuild the renderers:

**Option 1: Force rebuild (recommended)**

```bash
# Rebuild spatial renderer
rm -rf spatial_engine/spatialRender/build/
python -c "from src.config.configCPP import buildSpatialRenderer; buildSpatialRenderer()"

# Rebuild realtime engine
rm -rf spatial_engine/realtimeEngine/build/
python -c "from src.config.configCPP import buildRealtimeEngine; buildRealtimeEngine()"
```

**Option 2: Clean and rebuild**

```bash
# Clean and rebuild spatial renderer
cd spatial_engine/spatialRender/build/
make clean
make -j$(sysctl -n hw.ncpu)
cd ../../../

# Clean and rebuild realtime engine
cd spatial_engine/realtimeEngine/build/
make clean
make -j$(sysctl -n hw.ncpu)
cd ../../../
```

**Option 3: Manual CMake build**

```bash
# Full manual rebuild of spatial renderer
cd spatial_engine/spatialRender/
rm -rf build/
mkdir build && cd build/
cmake ..
make -j$(sysctl -n hw.ncpu)

# Full manual rebuild of realtime engine
cd spatial_engine/realtimeEngine/
rm -rf build/
mkdir build && cd build/
cmake ..
make -j$(sysctl -n hw.ncpu)
```

The built executables will be at:

- `spatial_engine/spatialRender/build/spatialroot_spatial_render`
- `spatial_engine/realtimeEngine/build/spatialroot_realtime`

## Manual Setup

If `init.sh` fails, you can set up manually:

```bash
# 1. Create virtual environment
python3 -m venv spatialroot

# 2. Install Python dependencies
spatialroot/bin/pip install -r requirements.txt

# 3. Initialize submodules and build all C++ tools (ADM extractor + renderer)
spatialroot/bin/python -c "from src.config.configCPP import setupCppTools; setupCppTools()"
```

## Utilities

- `init.sh` - One-time setup script (creates venv, installs dependencies, builds C++ tools, activates venv)
- `activate.sh` - Reactivates the virtual environment in new terminal sessions (use: `source activate.sh`)
- `utils/getExamples.py` - Downloads example ADM files
- `utils/deleteData.py` - Cleans processed data directory
- `src/config/configCPP.py` - C++ build utilities (use `buildSpatialRenderer()` and `buildRealtimeEngine()` to rebuild renderers)
- `gui/main.py` - Desktop GUI for offline pipeline configuration and execution
- `realtimeMain.py` - Command-line interface for realtime spatial audio engine

## Realtime Engine Overview

1. **Load Scene** - Parse LUSID JSON scene file
2. **Load Layout** - Read speaker layout and compute output channels
3. **Initialize Streaming** - Set up double-buffered audio streaming (mono stems or ADM direct)
4. **Initialize Pose** - Load keyframes and prepare interpolation
5. **Initialize Spatializer** - Build speakers and DBAP spatializer
6. **Start Audio I/O** - Open AlloLib audio device and begin real-time processing
7. **OSC Server** - Start parameter server for external control
8. **Process Blocks** - Real-time audio processing with spatialization

## Spatial Renderer Options

The spatial renderer supports multiple spatializers (DBAP, VBAP, LBAP) and render resolution modes:

| Mode     | Description                                              | Recommended       |
| -------- | -------------------------------------------------------- | ----------------- |
| `block`  | Compute direction once per block (default, blockSize=64) | ✓ Yes             |
| `sample` | Compute direction for every sample (highest accuracy)    | For critical work |
| `smooth` | _Deprecated_ - gain interpolation can cause artifacts    | No                |

### Command Line Usage

```bash
./spatialroot_spatial_render <input.json> <layout.json> <output.wav> [options]

Options:
  --render_resolution <mode>  Set render mode: block (recommended), sample, smooth
  --block_size <n>            Set block size for block mode (default: 64)
  --spatializer <type>        Set spatializer: dbap (default), vbap, lbap
```

### JSON Time Units

The LUSID scene JSON supports an explicit `timeUnit` field:

```json
{
  "sampleRate": 48000,
  "timeUnit": "seconds",
  "sources": [...]
}
```

Valid values: `"seconds"` (default), `"samples"`, `"milliseconds"`

For detailed documentation, see:

- [Spatialization/RENDERING.md](internalDocsMD/Spatialization/RENDERING.md) - Full rendering documentation
- [Dependencies/json_schema_info.md](internalDocsMD/Dependencies/json_schema_info.md) - JSON schema reference

## Testing Files

Example ADM files: https://zenodo.org/records/15268471

## Requirements

### Essential

- **Python 3.8+** - Core runtime for the Python components
- **CMake 3.12+** - Required to build the spatial audio renderer and embedded ADM extractor (C++17)
- **Build tools** - make, clang/gcc compiler toolchain

### Platform-specific notes

- **macOS**: Fully supported via `./init.sh`
- **Windows/Linux**: CMake + make/ninja required to build `spatialroot_adm_extract`

### ADM extraction

- **Primary**: `spatialroot_adm_extract` (embedded, built by `init.sh`) — no external install needed

---

## Offline Rendering Pipeline

The offline rendering pipeline provides batch processing capabilities for spatial audio rendering, now fully modernized to match the realtime engine's architecture and performance.

### Run the Pipeline

```bash
# ADM direct input (recommended - no intermediate files)
python runPipeline.py sourceData/driveExampleSpruce.wav

# LUSID package input (legacy mono WAV stems)
python runPipeline.py sourceData/lusid_package
```

**Command options:**

```bash
# Default mode (uses example ADM file)
python runPipeline.py

# With custom ADM file (direct streaming)
python runPipeline.py path/to/your_adm.wav

# With LUSID package (mono WAV folder)
python runPipeline.py path/to/lusid_package_folder

# Full options with custom layout
python runPipeline.py <input> <speaker_layout.json> <spatializer> <resolution> <master_gain> <analysis>
```

**Arguments:**

- `input` - ADM WAV file or LUSID package directory
- `speaker_layout.json` - Speaker layout JSON (default: `spatial_engine/speaker_layouts/allosphere_layout.json`)
- `spatializer` - Spatialization mode: `dbap`, `vbap`, or `lbap` (default: `dbap`)
- `resolution` - Spatial resolution parameter (default: `1.5`)
- `master_gain` - Master gain in dB (default: `0.5`)
- `analysis` - Create PDF analysis: `true` or `false` (default: `true`)

### Input Modes

**ADM Direct Input (Recommended):**

- Streams multichannel ADM WAV directly to spatial renderer
- No intermediate stem splitting required
- Uses `cult-transcoder` for ADM→LUSID conversion
- Fastest and most efficient workflow

**LUSID Package Input (Legacy):**

- Uses existing mono WAV stems with LUSID scene
- Maintains backward compatibility
- Requires pre-split audio stems

### Run the Desktop GUI

For a graphical interface to configure and run the offline pipeline:

```bash
python gui/main.py
```

The GUI provides file pickers, render settings, progress tracking, and log viewing.

### Spatial Rendering

The project supports three spatializers from AlloLib:

- **DBAP** (default) - Distance-Based Amplitude Panning, works with any layout
- **VBAP** - Vector Base Amplitude Panning, best for layouts with good 3D coverage
- **LBAP** - Layer-Based Amplitude Panning, designed for multi-ring layouts

See [`internalDocsMD/Offline_Rendering/offline_pipeline.md`](internalDocsMD/Offline_Rendering/offline_pipeline.md) for complete offline rendering documentation.

### Offline Pipeline Overview

**ADM Input Workflow:**

1. **ADM Preprocessing** - `cult-transcoder` extracts LUSID scene from ADM WAV
2. **Spatial Render** - Direct ADM streaming to spatial renderer
3. **Analysis** - Optional PDF analysis of spatial audio output

**LUSID Package Workflow:**

1. **Package Validation** - Verify LUSID scene and mono WAV stems
2. **Spatial Render** - Render mono stems using LUSID scene positions
3. **Analysis** - Optional PDF analysis of spatial audio output
