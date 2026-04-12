# Dependency Audit — Cross-Platform Build
**Date:** 2026-04-11  
**Session:** Windows compatibility investigation and fixes

---

## Vendored / Submoduled Dependencies

| Dependency | Location | Status |
|---|---|---|
| AlloLib (DBAP/VBAP/LBAP + rtaudio/rtmidi/GLFW/glad/imgui/oscpack) | `thirdparty/allolib` | submodule, `init.sh` initializes ✅ |
| libsndfile | `thirdparty/libsndfile` | submodule — was never initialized by `init.sh` or `init.ps1` ❌ → **fixed** |
| libbw64 (cult_transcoder) | `cult_transcoder/thirdparty/libbw64` | submodule, `init.sh` initializes ✅ |
| pugixml | FetchContent at configure-time | fetched from GitHub, needs internet ⚠️ |
| Catch2 | FetchContent at configure-time | fetched from GitHub, needs internet ⚠️ |

---

## System Dependencies by Platform

### macOS ✅
All dependencies provided by Xcode CLT + system frameworks. No extra `brew` installs required for non-GUI build.

- Xcode Command Line Tools — `clang++`, `make`, `libtool`
- CoreAudio, CoreFoundation — rtaudio
- CoreMIDI, CoreServices — rtmidi
- OpenGL framework — AlloLib `find_package(OpenGL REQUIRED)`

### Linux ✅ (CI passes)

| Package | Why required | Hard failure if missing? |
|---|---|---|
| `build-essential` | gcc/g++/make | yes |
| `libasound2-dev` | ALSA — rtaudio emits `FATAL_ERROR` if missing | yes |
| `libpulse-dev` | PulseAudio — rtaudio auto-enables if pkg-config finds it | soft |
| `libgl1-mesa-dev` + `libglu1-mesa-dev` | OpenGL — AlloLib `find_package(OpenGL REQUIRED)` | yes |
| `libx11-dev`, `libxrandr-dev`, `libxi-dev`, `libxinerama-dev`, `libxcursor-dev` | GLFW X11 backend — built even with `SPATIALROOT_BUILD_GUI=OFF` | yes |
| `pkg-config` | rtaudio/rtmidi use it for ALSA/JACK/Pulse detection | usually pre-installed |

### Windows ✅ (CI added this session)

Required toolchain:
- Visual Studio 2019+ with "Desktop development with C++" workload — provides MSVC compiler + Windows SDK (`ksuser`, `mfplat`, `mfuuid`, `wmcodecdspuuid`, `winmm`, `ole32`, `Ws2_32`, `setupapi`)
- CMake 3.20+
- Git

No additional package installs required — WASAPI and OpenGL are part of the Windows SDK.

---

## Other Notes

**Stale submodule entries:** `thirdparty/libbw64` and `thirdparty/libadm` are registered at the spatialroot level in `.gitmodules` but not used by any active `CMakeLists.txt` (`spatialroot_adm_extract` was archived in Phase 3). Not harmful but potentially confusing.

**FetchContent requires network:** `cult_transcoder/CMakeLists.txt` fetches Catch2 and pugixml from GitHub at first configure. Air-gapped builds will fail.

---

## Fixes Applied (2026-04-11)

### 1. `thirdparty/libsndfile` submodule not initialized
**Root cause:** `CMakeLists.txt:32` calls `add_subdirectory(thirdparty/libsndfile)` unconditionally but neither init script initialized the submodule. On a fresh clone the directory is empty and CMake fails immediately. macOS appeared to work only because the developer's clone had it checked out from a prior manual `git submodule update --init --recursive`.

**Files changed:**
- `init.sh` — added Step 4: initialize `thirdparty/libsndfile` (shallow, depth=1); renumbered imgui→5, glfw→6, build→7
- `init.ps1` — added Step 4: initialize `thirdparty/libsndfile` with error handling; renumbered build→5

---

### 2. Windows CI runner and build config
**Root cause:** No `windows-latest` runner in CI matrix; `cmake --build` lacked `--config Release` (required for Visual Studio's multi-config generator — without it MSVC defaults to Debug).

**Files changed:**
- `.github/workflows/ci.yml` — added `windows-latest` to matrix; added `--config Release` to cmake build step
- `build.ps1` — removed stale Qt reference from GUI comment

---

### 3. `init.ps1` compiler check
**Root cause:** `init.ps1` only verified `cmake` and `git` — no check that a C++ compiler was present, giving no actionable error on machines without Visual Studio.

**Files changed:**
- `init.ps1` — added Step 1 compiler check using `vswhere.exe` (detects VS + C++ workload); falls back to checking `cl.exe` on PATH; exits with install URL if neither found

---

### 4. `__builtin_popcountll` not available on MSVC
**Root cause:** `__builtin_popcountll` is a GCC/Clang compiler builtin. MSVC has no such builtin — it uses `__popcnt64` from `<intrin.h>`. Two call sites in `Spatializer.hpp` (lines 809, 928) failed with `C3861: identifier not found`.

**Fix:** Added a compat shim at the top of `Spatializer.hpp` guarded by `#ifdef _MSC_VER`:
```cpp
#ifdef _MSC_VER
#include <intrin.h>
static inline int sr_popcountll(unsigned long long x) { return static_cast<int>(__popcnt64(x)); }
#define __builtin_popcountll sr_popcountll
#endif
```
Existing call sites unchanged.

**Files changed:**
- `spatial_engine/realtimeEngine/src/Spatializer.hpp`

---

### 5. `std::filesystem::path` implicit conversion to `std::string` not available on MSVC
**Root cause:** GCC/Clang allow implicit conversion from `std::filesystem::path` to `std::string`. MSVC requires an explicit `.string()` call. Five call sites in `main.cpp` passed `fs::path` variables directly to functions expecting `const std::string&`, producing `C2664`.

**Files changed:**
- `spatial_engine/src/main.cpp` — added `.string()` at lines 231, 235, 242, 244, 256:
  - `LayoutLoader::loadLayout(layoutFile.string())`
  - `JSONLoader::loadLusidScene(positionsFile.string())`
  - `WavUtils::loadSourcesFromADM(admFile.string(), ...)`
  - `WavUtils::loadSources(sourcesFolder.string(), ...)`
  - `WavUtils::writeMultichannelWav(outFile.string(), output)`

---

### 6. `M_PI` not defined on MSVC without `_USE_MATH_DEFINES`
**Root cause:** `M_PI` is a POSIX extension. GCC/Clang define it in `<cmath>` unconditionally; MSVC only defines it if `_USE_MATH_DEFINES` is defined before the include. `test_360ra.cpp` used `M_PI` in three `const double` initializers, causing `C2065` (undeclared identifier) cascading into `C2737` (const object must be initialized).

**Files changed:**
- `cult_transcoder/tests/test_360ra.cpp` — added before `#include <cmath>`:
```cpp
#ifdef _MSC_VER
#  define _USE_MATH_DEFINES
#endif
```

---

### 7. `sscanf` deprecation warnings promoted to errors on MSVC
**Root cause:** MSVC flags `sscanf` as deprecated (C4996) with a suggestion to use `sscanf_s`. The `cult-transcoder` target has `/WX` (warnings as errors), so C4996 became `C2220` (error). Three call sites across two files were affected. Attempts to suppress via `_CRT_SECURE_NO_WARNINGS` in `target_compile_definitions` did not take effect reliably.

**Fix:** Applied `#pragma warning(suppress: 4996)` directly at each call site, guarded by `#ifdef _MSC_VER`. Zero change to parsing logic.

**Files changed:**
- `cult_transcoder/src/adm_to_lusid.cpp` — suppressed at `parseTimecode()` (line 95) and `floatToJsonString()` round-trip parse (line ~181)
- `cult_transcoder/transcoding/adm/sony360ra_to_lusid.cpp` — suppressed at `parseTimecode360Ra()` (line 114)
- `cult_transcoder/CMakeLists.txt` — `_CRT_SECURE_NO_WARNINGS` added to MSVC compile definitions for both `cult-transcoder` and `cult_tests` targets (belt-and-suspenders; pragma is the load-bearing fix)
