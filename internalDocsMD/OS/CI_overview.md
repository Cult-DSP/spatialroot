# CI Overview

## Version

CI v1 — build verification only.

## Workflow file

`.github/workflows/ci.yml`

---

## Triggers

- Push to `main` or `devel`
- Pull request targeting `main` or `devel`
- Manual dispatch (`workflow_dispatch`) from the GitHub Actions UI

---

## Platforms

| Runner label   | OS                       | Compiler              | Notes                                    |
| -------------- | ------------------------ | --------------------- | ---------------------------------------- |
| `ubuntu-22.04` | Ubuntu 22.04 LTS (Jammy) | gcc (build-essential) | Pinned label — will not silently upgrade |
| `macos-14`     | macOS 14 Sonoma          | Apple Clang           | Apple Silicon (M1). Pinned label.        |

`fail-fast: false` — both legs run to completion independently. Failures on both platforms are visible in a single run.

Runner labels are pinned (not `*-latest`) so CI failures are attributable to real repo changes, not silent image upgrades. Update the labels deliberately when needed.

---

## Build path

Direct CMake invocation — no wrapper scripts in CI. The steps mirror what `build.sh` does internally.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSPATIALROOT_BUILD_GUI=OFF
cmake --build build --parallel
```

**Targets built (all ON by default in CMakeLists.txt):**

- `spatialroot_realtime` — realtime spatial audio engine
- `spatialroot_spatial_render` — offline batch renderer
- `cult-transcoder` — ADM → LUSID transcoder

**GUI is explicitly disabled.** `gui/imgui/CMakeLists.txt` does not exist yet; enabling `SPATIALROOT_BUILD_GUI` would cause a CMake `FATAL_ERROR`.

---

## Submodules

`actions/checkout@v4` with `submodules: recursive` fetches all submodules in one step, including nested ones:

- `thirdparty/allolib` (shallow)
- `cult_transcoder` → `thirdparty/libbw64`, `thirdparty/libadm`
- `LUSID`
- `thirdparty/imgui`, `thirdparty/glfw` — checked out but unused (GUI off)

---

## Ubuntu system packages

AlloLib compiles OpenGL and audio I/O code unconditionally even when examples and tests are disabled. libsndfile is vendored so no system install is needed for it.

| Package                                                                         | Reason                                     |
| ------------------------------------------------------------------------------- | ------------------------------------------ |
| `build-essential`                                                               | gcc, g++, make                             |
| `libasound2-dev`, `libpulse-dev`                                                | ALSA/PulseAudio (RtAudio backend)          |
| `libgl1-mesa-dev`, `libglu1-mesa-dev`                                           | OpenGL headers (AlloLib links GL)          |
| `libx11-dev`, `libxrandr-dev`, `libxi-dev`, `libxinerama-dev`, `libxcursor-dev` | X11 headers (AlloLib + GLFW window system) |

macOS needs none of these — CoreAudio and OpenGL are system frameworks.

CMake 3.25+ is pre-installed on both runner images, satisfying the project's 3.20 minimum.

## Vendored dependencies

All C++ dependencies are git submodules — no system package manager installs are required for the core build:

| Library    | Path                                 | Purpose                                                      |
| ---------- | ------------------------------------ | ------------------------------------------------------------ |
| AlloLib    | `thirdparty/allolib`                 | Audio I/O, DBAP, OSC                                         |
| libsndfile | `thirdparty/libsndfile`              | WAV file I/O (`sndfile.h`); built static, no external codecs |
| libbw64    | `cult_transcoder/thirdparty/libbw64` | BW64 container reader (transcoder)                           |
| pugixml    | FetchContent (transcoder)            | XML parsing                                                  |
| LUSID      | `LUSID/`                             | Scene schema                                                 |

libsndfile is built with `ENABLE_EXTERNAL_LIBS=OFF` (no FLAC/Ogg/Vorbis/Opus) and `BUILD_PROGRAMS/EXAMPLES/TESTING=OFF`. It exports a `SndFile::sndfile` CMake target that both engine targets link against explicitly. The vendored build is also detected by AlloLib's bundled Gamma via pre-set `SNDFILE_INCLUDE_DIR`/`SNDFILE_LIBRARY` cache variables.

---

## Known CI build fixes

Bugs caught by the first CI runs and their resolutions:

### Fix 1 — `sndfile.h` not found (macOS + Ubuntu)
`spatial_engine/src/WavUtils.cpp` and the realtime engine include `<sndfile.h>` but libsndfile was never installed by CI and was not vendored. Fixed by adding `thirdparty/libsndfile` as a git submodule and wiring it into the root CMake build before AlloLib (see Vendored dependencies above).

### Fix 2 — `std::strcmp` not in `std` (Ubuntu / gcc)
`cult_transcoder/src/adm_to_lusid.cpp` called `std::strcmp` without including `<cstring>`. On macOS/clang this resolves transitively; on gcc it does not. Fixed by adding `#include <cstring>` to `adm_to_lusid.cpp`.

### Fix 3 — `libbw64` header warning promoted to error under `-Werror` (Ubuntu)
`cult_transcoder/CMakeLists.txt` declared libbw64 as a plain `INTERFACE` library, so gcc treated its headers as project headers and applied `-Werror`. `libbw64/include/bw64/parser.hpp:58` has a signed/unsigned comparison that became a hard error. Fixed by changing `target_include_directories(libbw64 INTERFACE ...)` to `target_include_directories(libbw64 SYSTEM INTERFACE ...)`, which marks the include path as a system header directory and suppresses third-party warnings.

---

## Scope and limitations (v1)

- **No audio device testing** — the realtime engine is built but not run; hardware I/O is untestable on headless runners.
- **No caching** — submodules and AlloLib build fresh each run. Revisit if build times become a problem.
- **No artifact upload** — binaries are discarded after the job. Build pass/fail is the only signal needed for v1.
- **No Windows** — PowerShell scripts (`build.ps1`, `init.ps1`) exist in the repo if Windows CI is added later.
- **No GUI** — excluded until `gui/imgui/` is implemented.

---

## Extending CI

When the time comes, natural next steps in rough priority order:

1. **Add Windows** (`windows-2022`, using `build.ps1` or direct cmake)
2. **Add build caching** (`actions/cache` on the `build/` directory or ccache)
3. **Add GUI build** once `gui/imgui/CMakeLists.txt` exists (requires GLFW/ImGui system deps on Ubuntu)
4. **Add smoke tests** if the binaries gain a `--help` or `--version` flag (cheap signal, no audio hardware needed)
5. **Add artifact upload** for binaries if integration or release testing becomes useful
