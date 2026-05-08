Cult DSP — Open Spatial Audio Infrastructure
Lead Developer: Lucian Parisi

# spatialroot

Spatial Root is a C++ spatial audio engine for decoding ADM BW64 files and rendering to multichannel speaker arrays using DBAP spatialization. It includes a real-time streaming engine, an offline batch renderer, and the CULT transcoder for ADM→LUSID scene conversion.

A C++ Dear ImGui + GLFW desktop GUI (`source/gui/imgui/`) is the replacement for the legacy Python GUI (archived/removed in Phase 6).

---

## Quick Start

### First Time Setup

Run **once** after cloning to initialize submodules and build all C++ components:

**macOS / Linux:**

```bash
git clone https://github.com/Cult-DSP/spatialroot.git
cd spatialroot
./init.sh
```

**Windows (PowerShell):**

```powershell
git clone https://github.com/Cult-DSP/spatialroot.git
cd spatialroot
Set-ExecutionPolicy -Scope Process Bypass
.\init.ps1
```

No Python toolchain required. Requires CMake 3.20+ and a C++17 compiler.

After setup, binaries are at:

| Binary                       | Path                                                            |
| ---------------------------- | --------------------------------------------------------------- |
| `spatialroot_realtime`       | `build/source/spatial_engine/realtimeEngine/spatialroot_realtime`      |
| `spatialroot_spatial_render` | `build/source/spatial_engine/spatialRender/spatialroot_spatial_render` |
| `cult-transcoder`            | `build/internal/cult_transcoder/cult-transcoder`                         |

### Subsequent Builds

```bash
./build.sh                  # Rebuild all components
./build.sh --engine-only    # Rebuild spatialroot_realtime only
./build.sh --offline-only   # Rebuild spatialroot_spatial_render only
./build.sh --cult-only      # Rebuild cult-transcoder only
```

---

## Realtime Engine — `spatialroot_realtime`

The primary entry point for live spatial audio playback.

### ADM workflow (recommended)

The engine requires a LUSID scene JSON file. For ADM input, use `cult-transcoder` to produce it first:

```bash
# Step 1: transcode ADM → LUSID scene
./build/internal/cult_transcoder/cult-transcoder transcode data/sourceData/myfile.wav

# Step 2: play with the realtime engine
./build/source/spatial_engine/realtimeEngine/spatialroot_realtime \
    --scene data/processedData/stageForRender/scene.lusid.json \
    --adm   data/sourceData/myfile.wav \
    --layout source/speaker_layouts/allosphere_layout.json
```

### LUSID package (mono stems) workflow

```bash
./build/source/spatial_engine/realtimeEngine/spatialroot_realtime \
    --scene   data/processedData/stageForRender/scene.lusid.json \
    --sources data/processedData/stageForRender/ \
    --layout  source/speaker_layouts/allosphere_layout.json
```

### All flags

```
Required:
  --layout <path>      Speaker layout JSON file
  --scene  <path>      LUSID scene JSON file (positions/trajectories)

Source input (one required):
  --sources <path>     Folder containing mono source WAV files
  --adm     <path>     Multichannel ADM WAV file

Optional:
  --samplerate <int>   Audio sample rate in Hz (default: 48000)
  --buffersize <int>   Frames per audio callback (default: 512)
  --gain <float>       Master gain 0.0–1.0 (default: 0.5)
  --focus <float>      DBAP rolloff exponent 0.2–5.0 (default: 1.5)
  --speaker_mix <dB>   Loudspeaker mix trim in dB (±10, default: 0)
  --sub_mix <dB>       Subwoofer mix trim in dB (±10, default: 0)
  --elevation_mode <n> Vertical rescaling: 0=RescaleAtmosUp, 1=RescaleFullSphere, 2=Clamp
  --remap <path>       CSV mapping internal layout channels to device channels
  --osc_port <int>     OSC control port (default: 9009; 0 = disable)
  --device <name>      Exact audio output device name
  --list-devices       List available output audio devices and exit
  --help               Show this message
```

### OSC parameter control

When `--osc_port` is non-zero (default: 9009), the engine accepts OSC messages on `127.0.0.1:<port>` for live parameter updates: `/realtime/gain`, `/realtime/focus`, `/realtime/speaker_mix_db`, `/realtime/sub_mix_db`, `/realtime/paused`, `/realtime/elevation_mode`.

### Quick dev rebuild (engine only)

```bash
./build.sh --engine-only
```

---

## CULT Transcoder — `cult-transcoder`

Converts ADM BW64 WAV files to the LUSID scene JSON format required by the engine.

```bash
# Transcode an ADM file → LUSID JSON
./build/internal/cult_transcoder/cult-transcoder transcode data/sourceData/myfile.wav

# Show all options
./build/internal/cult_transcoder/cult-transcoder --help
```

---

## Offline Renderer — `spatialroot_spatial_render`

Batch multichannel WAV rendering from a LUSID scene. Direct binary invocation is the post-refactor path.

```bash
./build/source/spatial_engine/spatialRender/spatialroot_spatial_render \
    <scene.lusid.json> <layout.json> <output.wav> [options]

Options:
  --render_resolution <mode>   block (default), sample, smooth
  --block_size <n>             Block size for block mode (default: 64)
  --spatializer <type>         dbap (default), lbap
```

---

## Build System

The build system is CMake + shell scripts. No Python required.

| Script      | Platform      | Role                                           |
| ----------- | ------------- | ---------------------------------------------- |
| `init.sh`   | macOS / Linux | Initialize submodules + call `build.sh`        |
| `build.sh`  | macOS / Linux | CMake configure + build                        |
| `init.ps1`  | Windows       | Initialize submodules + call `build.ps1`       |
| `build.ps1` | Windows       | CMake configure + build                        |
| `run.sh`    | macOS / Linux | Launch the ImGui GUI (builds first if needed)  |
| `run.ps1`   | Windows       | Launch the ImGui GUI                           |

The root `CMakeLists.txt` builds all components via option flags:

```cmake
SPATIALROOT_BUILD_ENGINE   ON   # spatialroot_realtime
SPATIALROOT_BUILD_OFFLINE  ON   # spatialroot_spatial_render
SPATIALROOT_BUILD_CULT     ON   # cult-transcoder
SPATIALROOT_BUILD_GUI      OFF  # ImGui + GLFW GUI (source/gui/imgui/)
```

Each component can also be built standalone from its own CMakeLists.txt.

### Requirements

- **CMake 3.20+**
- **C++17 compiler**: clang on macOS, gcc or clang on Linux, MSVC or clang-cl on Windows
- **make / ninja** (macOS/Linux) or **MSBuild / Ninja** (Windows)
- **git** (for submodule initialization)

All C++ dependencies (AlloLib, libsndfile, libbw64, pugixml, LUSID) are vendored as git submodules. No system package manager installs are required for the core build.

---

## Rebuilding after C++ source changes

```bash
# Quick rebuild — realtime engine only
./build.sh --engine-only

# Full rebuild — all components
./build.sh
```

---

## Example Files

Example ADM files: https://zenodo.org/records/15268471

---

## Public API

The realtime engine exposes a C++ embedding API (`EngineSessionCore` static library). See [PUBLIC_DOCS/API.md](PUBLIC_DOCS/API.md) for full documentation.

---

## C++ GUI

The Dear ImGui + GLFW desktop GUI at `source/gui/imgui/` is the primary GUI. Build it with:

```bash
./build.sh --gui          # macOS / Linux
.\build.ps1 -GuiBuild     # Windows
```

Then launch from the project root:

```bash
./run.sh                  # macOS / Linux
.\run.ps1                 # Windows
```

The GUI controls the spatial audio engine directly via the `EngineSessionCore` C++ API (no OSC). It supports ADM WAV and LUSID package sources, speaker layout selection, and real-time parameter control.

Generated GUI-owned scenes now use an app-cache temp-session root instead of normal `data/processedData/` paths:

- macOS: `~/Library/Caches/CultDSP/SpatialRoot/temp-sessions/`
- Linux: `$XDG_CACHE_HOME/CultDSP/SpatialRoot/temp-sessions/` or `~/.cache/CultDSP/SpatialRoot/temp-sessions/`
- Windows: `%LOCALAPPDATA%/CultDSP/SpatialRoot/Cache/temp-sessions/`

Each temp session is created as `session_<timestamp>_<shortid>/` and includes `.spatialroot_temp_session` and `manifest.json`. Generated `scene.lusid.json` and `reports/` are added only when that workflow produces them.

Temporary generated sessions are deleted when the app closes unless you preserve them for debugging. Use `Save Generated Scene...` to copy generated output to a durable location, or `Save Diagnostic Files...` after a failed import/transcode. Developers can pass `--keep-temp-sessions` to the GUI, or use `--temp-root <path>` as a temp-root override.

## Project Structure

```
spatialroot/
├── source/
│   ├── gui/
│   │   └── imgui/          # C++ Dear ImGui + GLFW desktop GUI (primary)
│   ├── scripts/            # Repo-level developer helper scripts
│   └── spatial_engine/
│       ├── realtimeEngine/ # spatialroot_realtime engine + EngineSessionCore
│       ├── spatialRender/  # spatialroot_spatial_render offline renderer
│       ├── speaker_layouts/
│       └── src/            # Shared loaders (JSONLoader, LayoutLoader, WavUtils)
├── internal/
│   ├── cult_transcoder/    # cult-transcoder (git submodule, standalone)
│   ├── LUSID/              # LUSID schema + docs (git submodule)
│   └── cult-allolib/       # Internal AlloLib fork
├── docs/                   # If/when public docs are split from repo root docs
├── examples/               # If/when shared examples are promoted from subtrees
├── thirdparty/             # Additional vendored dependencies
├── data/
│   ├── processedData/      # Working outputs
│   └── sourceData/         # Input audio + LUSID packages
├── PUBLIC_DOCS/API.md      # EngineSession C++ embedding API documentation
├── internalDocs/           # Internal onboarding, build, engine, and history notes
├── CMakeLists.txt          # Root build — all components
├── build.sh / init.sh      # macOS/Linux build scripts
└── build.ps1 / init.ps1    # Windows build scripts
```
