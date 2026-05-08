# Build System & CI — Internal Reference

**Last Updated:** April 17, 2026  
**Source files:** `init.sh`, `build.sh`, `init.ps1`, `build.ps1`, `.github/workflows/ci.yml`

---

## CI Overview

> `CI_overview.md` — CI v1, build verification only.

### Workflow

File: `.github/workflows/ci.yml`

**Triggers:** Push to `main`/`devel`, PRs targeting `main`/`devel`, manual `workflow_dispatch`.

**Platforms:**

| Runner           | OS               | Compiler    | Notes                           |
| ---------------- | ---------------- | ----------- | ------------------------------- |
| `ubuntu-22.04`   | Ubuntu 22.04 LTS | gcc         | Pinned — won't silently upgrade |
| `macos-14`       | macOS 14 Sonoma  | Apple Clang | Apple Silicon (M1). Pinned.     |
| `windows-latest` | Windows          | MSVC        | Added April 2026                |

`fail-fast: false` — all legs run to completion independently.

### Build Steps

Direct CMake — no wrapper scripts in CI:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSPATIALROOT_BUILD_GUI=OFF
cmake --build build --parallel
```

**Targets built (all ON by default):**

- `spatialroot_realtime` — realtime spatial audio engine
- `spatialroot_spatial_render` — offline batch renderer
- `cult-transcoder` — ADM → LUSID transcoder

**GUI is explicitly disabled.** The GUI build exists, but it is kept off in CI pending integration verification.

### Submodules

`actions/checkout@v4` with `submodules: recursive`:

- `internal/cult-allolib`
- `cult_transcoder` → `thirdparty/libbw64`, `thirdparty/libadm`
- `LUSID`
- `thirdparty/imgui`, `thirdparty/glfw` — checked out but unused (GUI off)

### Ubuntu System Packages

AlloLib compiles OpenGL and audio I/O code unconditionally even when examples/tests are disabled. libsndfile is vendored.

| Package                                                                         | Reason                            |
| ------------------------------------------------------------------------------- | --------------------------------- |
| `build-essential`                                                               | gcc, g++, make                    |
| `libasound2-dev`, `libpulse-dev`                                                | ALSA/PulseAudio (RtAudio backend) |
| `libgl1-mesa-dev`, `libglu1-mesa-dev`                                           | OpenGL headers (AlloLib links GL) |
| `libx11-dev`, `libxrandr-dev`, `libxi-dev`, `libxinerama-dev`, `libxcursor-dev` | X11 headers (AlloLib + GLFW)      |

macOS needs none of these — CoreAudio and OpenGL are system frameworks.

### Vendored Dependencies

All C++ dependencies are git submodules — no package manager installs required:

| Library    | Path                                 | Purpose                                        |
| ---------- | ------------------------------------ | ---------------------------------------------- |
| AlloLib    | `internal/cult-allolib`              | Internal fork: audio I/O, DBAP, OSC            |
| libsndfile | `thirdparty/libsndfile`              | WAV file I/O; built static, no external codecs |
| libbw64    | `cult_transcoder/thirdparty/libbw64` | BW64 container reader (transcoder)             |
| pugixml    | FetchContent (transcoder)            | XML parsing                                    |
| LUSID      | `LUSID/`                             | Scene schema                                   |

libsndfile is built with `ENABLE_EXTERNAL_LIBS=OFF` and `BUILD_PROGRAMS/EXAMPLES/TESTING=OFF`. Exports `SndFile::sndfile` CMake target. The vendored build is also detected by AlloLib's Gamma via pre-set `SNDFILE_INCLUDE_DIR`/`SNDFILE_LIBRARY` cache variables.

### Known CI Build Fixes

**Fix 1 — `sndfile.h` not found (macOS + Ubuntu):** `thirdparty/libsndfile` was never initialized or vendored. Fixed by adding it as a git submodule and wiring into root CMake before AlloLib.

**Fix 2 — `std::strcmp` not in `std` (Ubuntu/gcc):** `cult_transcoder/src/adm_to_lusid.cpp` called `std::strcmp` without `<cstring>`. Resolves transitively on macOS/clang but not gcc. Fixed with `#include <cstring>`.

**Fix 3 — libbw64 header warning → error under `-Werror` (Ubuntu):** `libbw64` declared as plain `INTERFACE` library, so gcc treated headers as project headers. `bw64/parser.hpp:58` has a signed/unsigned comparison that became a hard error. Fixed: `target_include_directories(libbw64 SYSTEM INTERFACE ...)`.

### Scope and Limitations (v1)

- No audio device testing — hardware I/O untestable on headless runners
- No build caching — submodules and AlloLib rebuild fresh each run
- No artifact upload — binaries discarded after job
- No GUI — excluded until `gui/imgui/` is implemented

### Extending CI

Natural next steps in priority order:

1. Add build caching (`actions/cache` on `build/` or ccache)
2. Add GUI build (CMakeLists exists; verify integration and runner requirements)
3. Add smoke tests if binaries gain `--help`/`--version` flags
4. Add artifact upload for release testing

---

## Dependency Audit

> `3_11_dep_audit.md` — Cross-platform build audit, April 11, 2026.

### System Dependencies by Platform

**macOS** — all provided by Xcode CLT + system frameworks. No `brew` installs required for non-GUI build.

- Xcode Command Line Tools (`clang++`, `make`, `libtool`)
- CoreAudio, CoreFoundation (rtaudio)
- CoreMIDI, CoreServices (rtmidi)
- OpenGL framework (AlloLib)

**Linux** — see Ubuntu system packages table above. All CI-verified.

**Windows** — Required toolchain:

- Visual Studio 2019+ with "Desktop development with C++" workload
- CMake 3.20+
- Git

No additional package installs — WASAPI and OpenGL are part of the Windows SDK.

### Fixes Applied April 11, 2026

**1. `thirdparty/libsndfile` not initialized**
`CMakeLists.txt:32` calls `add_subdirectory(thirdparty/libsndfile)` unconditionally but neither init script initialized the submodule. Fresh clones failed. Fixed: `init.sh` Step 4 initializes `thirdparty/libsndfile` (shallow, depth=1). `init.ps1` Step 4 likewise.

**2. Windows CI runner and build config**
No `windows-latest` runner existed. `cmake --build` lacked `--config Release` (MSVC multi-config generator defaults to Debug without it). Fixed: `ci.yml` added `windows-latest`; added `--config Release`.

**3. `init.ps1` compiler check**
`init.ps1` only verified `cmake` and `git`. Added Step 1 compiler check using `vswhere.exe` (detects VS + C++ workload); falls back to `cl.exe` on PATH; exits with install URL if neither found.

**4. `__builtin_popcountll` not available on MSVC**
`__builtin_popcountll` is GCC/Clang only. MSVC uses `__popcnt64` from `<intrin.h>`. Two call sites in `Spatializer.hpp` (lines 809, 928). Fixed: compat shim at top of `Spatializer.hpp`:

```cpp
#ifdef _MSC_VER
#include <intrin.h>
static inline int sr_popcountll(unsigned long long x) { return static_cast<int>(__popcnt64(x)); }
#define __builtin_popcountll sr_popcountll
#endif
```

**5. `std::filesystem::path` implicit `std::string` conversion (MSVC)**
MSVC requires explicit `.string()` call. Five call sites in `main.cpp` (lines 231, 235, 242, 244, 256). Fixed: added `.string()` at each call site.

**6. `M_PI` not defined on MSVC without `_USE_MATH_DEFINES`**
`test_360ra.cpp` used `M_PI` in three `const double` initializers. Fixed: added before `#include <cmath>`:

```cpp
#ifdef _MSC_VER
#  define _USE_MATH_DEFINES
#endif
```

**7. `sscanf` deprecation warnings → errors on MSVC (`/WX`)**
`cult-transcoder` target has `/WX`. `sscanf` is C4996 (deprecated) on MSVC → C2220 (error). Applied `#pragma warning(suppress: 4996)` at each call site (3 sites across 2 files), guarded by `#ifdef _MSC_VER`. Also added `_CRT_SECURE_NO_WARNINGS` to `CMakeLists.txt` compile definitions (belt-and-suspenders).

### Windows Fixes Applied April 17, 2026

**1. `init.ps1` still initialized `thirdparty/allolib` after the fork switch**
Root CMake had already moved to `internal/cult-allolib`, but the Windows bootstrap still checked and fetched `thirdparty/allolib`. Fresh Windows clones could therefore fail before the active AlloLib fork was initialized. Fixed: `init.ps1` Step 2 now checks `internal\cult-allolib\include` and initializes `internal/cult-allolib`.

**2. `init.ps1` did not initialize GUI submodules**
The PowerShell init path always invoked `build.ps1 -GuiBuild` but did not initialize `thirdparty/imgui` or `thirdparty/glfw`. Fixed: `init.ps1` now mirrors `init.sh` and conditionally initializes `thirdparty/imgui` and `thirdparty/glfw` when they are registered in `.gitmodules`.

**3. GUI logo embedding depended on `xxd`**
`gui/imgui/CMakeLists.txt` previously used `xxd -i` to generate `miniLogo_data.h`. `xxd` is common on Unix-like systems but not a standard Windows/Visual Studio tool. Fixed: logo embedding now uses a portable CMake script (`gui/imgui/cmake/EmbedBinaryAsHeader.cmake`), removing the external `xxd` dependency from Windows GUI builds.

**4. Live engine / renderer files still used `M_PI` without MSVC opt-in**
Some active realtime and offline spatialization files still used `M_PI` directly, but only a historical test-file fix was documented. MSVC requires `_USE_MATH_DEFINES` before `<cmath>` for `M_PI` to exist. Fixed in the active code paths by adding the MSVC guard before `<cmath>` in:

- `spatial_engine/realtimeEngine/src/Pose.hpp`
- `spatial_engine/realtimeEngine/src/Spatializer.hpp`
- `spatial_engine/src/renderer/SpatialRenderer.cpp`
- `spatial_engine/src/vbap_src/VBAPRenderer.cpp`

### Stale Submodule Entries

`thirdparty/libbw64` and `thirdparty/libadm` are registered in `.gitmodules` at the spatialroot level but not used by any active `CMakeLists.txt` (`spatialroot_adm_extract` was archived in Phase 3). Not harmful but potentially confusing.

### FetchContent Requires Network

`cult_transcoder/CMakeLists.txt` fetches Catch2 (v3.5.3) and pugixml (v1.14) from GitHub at first configure. Air-gapped builds will fail on first run.

---

## Build System Notes

> `build-wiring.md` — CMake wiring and submodule nesting, March 7, 2026.

### CMake FetchContent Dependencies (cult-transcoder)

| Dependency | Version | Source                                   |
| ---------- | ------- | ---------------------------------------- |
| Catch2     | v3.5.3  | `https://github.com/catchorg/Catch2.git` |
| pugixml    | v1.14   | `https://github.com/zeux/pugixml.git`    |

Fetched into `cult_transcoder/build/_deps/` at configure time. Network required on first run.

### cult-transcoder Submodule Nesting

`cult_transcoder` is a git submodule of spatialroot **and** has its own nested submodule (`thirdparty/libbw64`). From spatialroot root:

```bash
git submodule update --init cult_transcoder
cd cult_transcoder
git submodule update --init thirdparty/libbw64
```

`init.sh` / `init.ps1` handle this automatically via `initializeCultTranscoderSubmodules()` which runs `git submodule update --init --depth 1 thirdparty/libbw64` from within `cult_transcoder/`.

Presence check: `cult_transcoder/thirdparty/libbw64/include/bw64/bw64.hpp` — the same path that `cult_transcoder/CMakeLists.txt` checks in its `FATAL_ERROR` guard.

### cult-transcoder Binary Paths

- macOS/Linux: `build/cult_transcoder/cult-transcoder`
- Windows: `build/cult_transcoder/Release/cult-transcoder.exe` (VS multi-config) or `build/cult_transcoder/cult-transcoder.exe` (Ninja single-config); check both.

### SPATIALROOT_BUILD_GUI Flag

`SPATIALROOT_BUILD_GUI=OFF` (default) disables GUI build. Enable with `SPATIALROOT_BUILD_GUI=ON`. GUI build is not yet enabled in CI — verify `gui/imgui/CMakeLists.txt` integration before enabling there.

### Current Bootstrap Expectations

**macOS / Linux**

- `init.sh` initializes `internal/cult-allolib`, `cult_transcoder` plus nested dependencies, `thirdparty/libsndfile`, and optional GUI submodules when they are registered
- GUI-enabled builds use the same portable logo-embedding path as Windows; no `xxd` dependency remains

**Windows**

- `init.ps1` initializes `internal/cult-allolib`, `cult_transcoder` plus nested dependencies, `thirdparty/libsndfile`, and optional GUI submodules when they are registered
- `build.ps1` remains the canonical Windows build entry point
- GUI-enabled builds no longer require `xxd`; logo embedding is handled entirely by CMake

### Known Windows Limitations

- `build.ps1` passes both `-DCMAKE_BUILD_TYPE=Release` and `--config Release`; on Visual Studio generators only `--config Release` is authoritative
- GUI remains disabled in CI, so the Windows GUI bootstrap path is now self-contained but not yet continuously verified by CI
