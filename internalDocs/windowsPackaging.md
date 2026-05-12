# Spatial Root — Windows Packaging

**Session date:** May 12, 2026
**Branch:** `windows`
**Agent scope:** First-pass Windows x64 portable ZIP package via CMake install rules + CPack + GitHub Actions.

---

## Goal

Produce a self-contained, portable Windows x64 ZIP that a user can extract and launch without the source repo or build tree present.

This is a first milestone. Signing, NSIS/MSI, winget, and Microsoft Store packaging are explicitly out of scope.

---

## Package Layout

After `cmake --install ... --component SpatialRootRuntime --prefix <dir>`:

```
<prefix>/
  SpatialRoot.exe                   ← GUI + embedded audio engine (EngineSessionCore)
  spatialroot_spatial_render.exe    ← offline batch renderer (subprocess)
  cult-transcoder.exe               ← ADM/WAV transcoder (subprocess)
  resources/
    speaker_layouts/                ← JSON speaker preset files
      stereo.json
      quad.json
      ...
  README.md
```

The CPack ZIP (`SpatialRoot-alpha-v1-windows-x64.zip`) contains these files flat at archive root (no top-level subdirectory — extract into a folder of your choice).

---

## Build System

- CMake 3.20+, Visual Studio 17 2022, x64, Release
- CPack ZIP generator (`include(CPack)` gated on `WIN32 AND SPATIALROOT_BUILD_GUI`)
- All runtime install rules use component `SpatialRootRuntime`

---

## Files Changed

| File | Change |
|------|--------|
| [CMakeLists.txt](../CMakeLists.txt) | Added CPack ZIP config at end, gated on `WIN32 AND SPATIALROOT_BUILD_GUI` |
| [source/gui/imgui/CMakeLists.txt](../source/gui/imgui/CMakeLists.txt) | Windows output name `SpatialRoot` (no space); Windows install exe to `.`, layouts to `resources/`, README to `.` |
| [internal/cult_transcoder/CMakeLists.txt](../internal/cult_transcoder/CMakeLists.txt) | Windows: install `cult-transcoder.exe` to `.` instead of `bin/` |
| [source/spatial_engine/spatialRender/CMakeLists.txt](../source/spatial_engine/spatialRender/CMakeLists.txt) | Added first-time install rules: Windows to `.`, Linux to `bin/` |
| [source/gui/imgui/src/App.cpp](../source/gui/imgui/src/App.cpp) | `findSpatialRenderer()` now checks exe-relative paths before build-tree fallback |
| [source/spatial_engine/spatialRender/SpatialRenderer.cpp](../source/spatial_engine/spatialRender/SpatialRenderer.cpp) | Fixed hardcoded `"/"` path join on debug stats output file |
| [.github/workflows/windows-package.yml](../.github/workflows/windows-package.yml) | New workflow: build + install + verify + CPack ZIP + upload artifact |

---

## GitHub Actions Workflow

**File:** `.github/workflows/windows-package.yml`
**Name:** `Windows Package`

**Triggers:**
- `workflow_dispatch` (manual)
- Push to branch `windows`

**Runner:** `windows-2022`

**Steps:**
1. Checkout with `submodules: recursive`
2. Configure: VS 17 2022, x64, Release, all four main components ON
3. Build: Release config, parallel
4. `cmake --install --component SpatialRootRuntime --prefix stage/`
5. Verify staged files (SpatialRoot.exe, spatialroot_spatial_render.exe, cult-transcoder.exe, resources/)
6. `cpack --config build-windows/CPackConfig.cmake -G ZIP -C Release`
7. Upload `*.zip` as artifact `SpatialRoot-alpha-v1-windows-x64`

---

## Build Commands (manual)

```pwsh
# Configure
cmake -S . -B build-windows `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DSPATIALROOT_BUILD_GUI=ON `
  -DSPATIALROOT_BUILD_ENGINE=ON `
  -DSPATIALROOT_BUILD_OFFLINE=ON `
  -DSPATIALROOT_BUILD_CULT=ON

# Build
cmake --build build-windows --config Release --parallel

# Stage (for inspection)
cmake --install build-windows --config Release --component SpatialRootRuntime --prefix stage

# ZIP
cpack --config build-windows/CPackConfig.cmake -G ZIP -C Release
```

---

## Helper Binary Discovery

### `cult-transcoder.exe`

1. `SPATIALROOT_CULT_TRANSCODER` env var
2. `<exeDir>/cult-transcoder.exe` ← flat Windows package
3. `<exeDir>/../libexec/spatialroot/cult-transcoder.exe`
4. macOS bundle `Contents/Resources/bin/cult-transcoder` (not applicable on Windows)
5. Build-tree dev fallbacks

### `spatialroot_spatial_render.exe`

1. `SPATIALROOT_SPATIAL_RENDER` env var
2. `<exeDir>/spatialroot_spatial_render.exe` ← flat Windows package (new in this pass)
3. `<exeDir>/../bin/spatialroot_spatial_render.exe` ← FHS-style installs
4. Build-tree dev fallback

### Speaker layouts

1. `SPATIALROOT_ASSET_ROOT` env var
2. macOS bundle `Contents/Resources/speaker_layouts/` (not applicable on Windows)
3. `<exeDir>/speaker_layouts/` ← packaged fallback
4. `<exeDir>/../share/spatialroot/speaker_layouts/`
5. Repo-root `source/speaker_layouts/` (dev fallback)

**Note:** For the Windows flat package, layouts are at `resources/speaker_layouts/`, not directly at `<exeDir>/speaker_layouts/`. The existing discovery order covers `<exeDir>/../share/spatialroot/speaker_layouts/` (FHS) and `<exeDir>/speaker_layouts/` (flat), but NOT `<exeDir>/resources/speaker_layouts/`. This means layout discovery from the package will fall through to the dev fallback if run without setting `SPATIALROOT_ASSET_ROOT`. See "Known Untested / Open Issues" below.

---

## Known Working State

- CMake configure, build, install, and CPack ZIP steps authored correctly on macOS and designed to run on Windows runner
- macOS install/bundle behavior is unchanged
- Linux install rules for all three binaries are unchanged or newly wired

---

## Known Untested / Open Issues

| Item | Status |
|------|--------|
| Actual Windows build + link + package — not run yet | Needs first CI run on `windows` branch |
| Runtime DLLs (MSVC CRT, OpenGL, AlloLib/RtAudio deps) | Unknown — inspect staged tree after first successful build; may need `INSTALL_RUNTIME_DEPENDENCIES` or vcredist |
| Layout discovery from installed package | `resources/speaker_layouts/` path not in current search list; layouts will fall back to dev path. Set `SPATIALROOT_ASSET_ROOT=<stage>/resources` as workaround until App.cpp search order is updated |
| Audio backend on Windows (WASAPI/ASIO via RtAudio) | Not validated |
| GLFW/OpenGL on headless runner | Build only, not launch tested |
| `Spatial Root.exe` → `SpatialRoot.exe` name change | macOS bundle still uses "Spatial Root"; Windows output name is now "SpatialRoot" |
| Windows signing / SmartScreen | Not addressed in this pass |

---

## Layout Discovery Fix Needed (Post-First-Build)

After the first successful Windows build, update the layout search candidates in `App.cpp` to include the Windows package `resources/` path:

```cpp
// In resolveProjectPath / layout candidate list, add:
if (const fs::path exeDir = executableDirectory(); !exeDir.empty()) {
    appendCandidate(candidates, exeDir / "resources" / kPackagedLayoutRoot / packagedSubpath);
}
```

This is deferred until after the first successful build confirms the overall structure.

---

## Validation Checklist (Manual — on real Windows machine)

- [ ] Download `SpatialRoot-alpha-v1-windows-x64.zip` from GitHub Actions artifact
- [ ] Extract to a path **with spaces**, e.g. `C:\Users\Lucian\Downloads\Spatial Root Alpha Test\`
- [ ] Double-click `SpatialRoot.exe` — confirm GUI opens
- [ ] Confirm bundled fonts and icons load (no missing resource errors)
- [ ] Confirm default layouts are visible or loadable (or set `SPATIALROOT_ASSET_ROOT` if not)
- [ ] Confirm helper binaries are found (check log panel in GUI)
- [ ] Confirm CULT transcode either works or fails with a descriptive error message
- [ ] Confirm audio backend/device failures are descriptive and do not crash the app
- [ ] Confirm no dependency on the source repo being present
- [ ] Confirm no dependency on the build folder being present

---

## Explicit Non-Goals

- NSIS installer
- MSI / WiX
- winget manifest
- Microsoft Store packaging
- macOS cross-compilation of Windows binaries
- Code signing / Authenticode
- macOS packaging changes (preserved as-is)
