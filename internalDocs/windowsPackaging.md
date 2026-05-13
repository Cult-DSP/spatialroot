# Spatial Root — Windows Packaging

**Session date:** May 13, 2026
**Branch:** `windows`
**Status:** Windows portable ZIP hardening pass

---

## Original Failure

The first Windows CI build succeeded and produced `SpatialRoot-alpha-v1-windows-x64.zip`, but the extracted app failed on a clean Windows machine with:

- `msvcp140.dll not found`
- `vcruntime140.dll not found`
- `vcruntime140_1.dll not found`

That failure confirmed an important packaging gap: CI build success did not prove clean-machine redistributability. The binaries linked against the dynamic Microsoft Visual C++ runtime, but the portable ZIP did not provide those redistributable runtime DLLs app-locally.

---

## Chosen Strategy

**Chosen:** Strategy B, app-local VC runtime DLL bundling.

**Why this was chosen:**

- It directly fixes the reported clean-machine failure without adding an installer.
- It preserves the required first artifact shape: a portable ZIP.
- It uses Microsoft’s official redistributable runtime discovery instead of copying random DLLs from `System32`.
- It is straightforward to validate in CI by checking both staged files and actual runtime dependency resolution.

**Implementation mechanism:**

- Top-level [CMakeLists.txt](../CMakeLists.txt) now uses CMake’s `InstallRequiredSystemLibraries` module on Windows packaging builds.
- The staged DLLs come from the Visual Studio / MSVC redistributable locations discovered by that module on the `windows-2022` runner.
- The install destination is the package root, beside `SpatialRoot.exe`.

**Expected app-local DLLs in the ZIP:**

- `msvcp140.dll`
- `vcruntime140.dll`
- `vcruntime140_1.dll`
- Any additional Microsoft redistributable runtime files that `InstallRequiredSystemLibraries` determines are required for that toolchain configuration

---

## Rejected Strategies

### Strategy A — static MSVC runtime linking

Investigated first, but not selected for this pass.

Why it was not chosen yet:

- The Windows package needs one reliable clean-machine path across the GUI, `spatialroot_spatial_render`, `cult-transcoder`, and the vendored / FetchContent-built dependency graph.
- Applying `/MT` consistently across Spatial Root-owned targets is manageable, but validating every participating third-party sub-build and avoiding mixed-runtime linker conflicts is riskier without a Windows validation pass dedicated to that change.
- App-local bundling is lower-risk for the alpha ZIP and is easier to verify automatically in CI.

### Strategy C — documented VC++ prerequisite only

Rejected as the primary alpha experience.

Why:

- It would keep the ZIP non-portable from a user’s perspective.
- It pushes a confusing prerequisite onto testers.
- A better out-of-the-box path is available now.

---

## Package Layout

After `cmake --install --config Release --component SpatialRootRuntime --prefix <stage>`:

```text
<stage>/
  SpatialRoot.exe
  spatialroot_spatial_render.exe
  cult-transcoder.exe
  msvcp140.dll
  vcruntime140.dll
  vcruntime140_1.dll
  [other official MSVC redist DLLs as required]
  resources/
    speaker_layouts/
      allosphere_layout.json
      stereo.json
      ...
  README.md
  LICENSE
  SpatialRoot-startup.log              # created at runtime
  SpatialRoot-package-self-test.log    # created by --package-self-test
```

The produced ZIP remains:

- `SpatialRoot-alpha-v1-windows-x64.zip`
- flat at archive root, with no extra top-level wrapper directory

---

## Dependency Bundling Policy

### Microsoft runtime

- Bundle official Microsoft redistributable runtime DLLs app-locally beside the EXEs.
- Do not copy runtime files from `C:\Windows\System32`.
- Do not ship the full `vc_redist` installer inside the ZIP for the normal alpha flow.

### Third-party runtime dependencies

- Bundle third-party DLLs required for normal launch if licensing permits.
- Do not rely on developer-machine global installs.
- Do not bundle Windows system DLLs.

### Current dependency audit result

- The explicit original failure was the dynamic MSVC runtime.
- CI now performs a dependency scan over:
  - `SpatialRoot.exe`
  - `cult-transcoder.exe`
  - `spatialroot_spatial_render.exe`
- The scan fails if required non-system runtime dependencies are unresolved.

---

## Package Self-Test

`SpatialRoot.exe` now supports:

```powershell
.\SpatialRoot.exe --package-self-test
```

The self-test reports:

- executable path
- app root
- resources path
- whether resources exist
- layouts path
- whether layouts exist
- `cult-transcoder` path
- whether `cult-transcoder` exists
- `spatialroot_spatial_render` path
- whether it exists
- current working directory
- writable temp/cache path
- whether a temp/cache test file can be written
- whether known app-local VC runtime DLLs are present

It also writes:

- `SpatialRoot-package-self-test.log`

---

## Startup Logging

Alpha Windows builds now write:

- `SpatialRoot-startup.log`

The log records:

- startup entered
- resolved executable path
- resolved app root
- resolved resources directory
- helper binary checks
- GUI init start
- audio/backend init requested
- audio/backend init start
- last successful startup step before failure, when startup/engine init fails

---

## CI Workflow

**Workflow:** [.github/workflows/windows-package.yml](../.github/workflows/windows-package.yml)

Current Windows packaging flow:

1. Checkout with submodules on `windows-2022`
2. Configure Visual Studio 2022 x64 Release with GUI, engine, offline renderer, and transcoder enabled
3. Build Release
4. Install `SpatialRootRuntime` into `stage/`
5. Verify staged package files exist
6. Verify app-local VC runtime DLLs exist
7. Run [cmake/VerifyWindowsPackage.cmake](../cmake/VerifyWindowsPackage.cmake) to resolve runtime dependencies from the staged package
8. Run `.\SpatialRoot.exe --package-self-test`
9. Produce the ZIP with CPack
10. Upload artifact `SpatialRoot-alpha-v1-windows-x64`

---

## Files Changed In This Pass

- [CMakeLists.txt](../CMakeLists.txt)
- [source/gui/imgui/CMakeLists.txt](../source/gui/imgui/CMakeLists.txt)
- [source/gui/imgui/src/main.cpp](../source/gui/imgui/src/main.cpp)
- [source/gui/imgui/src/App.cpp](../source/gui/imgui/src/App.cpp)
- [source/gui/imgui/src/StartupLogger.hpp](../source/gui/imgui/src/StartupLogger.hpp)
- [source/gui/imgui/src/StartupLogger.cpp](../source/gui/imgui/src/StartupLogger.cpp)
- [cmake/VerifyWindowsPackage.cmake](../cmake/VerifyWindowsPackage.cmake)
- [.github/workflows/windows-package.yml](../.github/workflows/windows-package.yml)
- [README.md](../README.md)
- [internalDocs/AGENTS.md](../internalDocs/AGENTS.md)
- [internalDocs/devHistory.md](../internalDocs/devHistory.md)
- [LICENSE](../LICENSE)

---

## Clean-Machine Validation Checklist

Target extraction path:

```text
C:\Users\Lucian\Downloads\Spatial Root Alpha Test\
```

Manual validation steps:

1. Download `SpatialRoot-alpha-v1-windows-x64.zip`
2. Extract into the path above
3. Run:

```powershell
cd "C:\Users\Lucian\Downloads\Spatial Root Alpha Test"
.\SpatialRoot.exe --package-self-test
```

4. Double-click `SpatialRoot.exe`
5. Confirm the GUI opens
6. Confirm no missing MSVC runtime DLL dialog appears
7. Confirm layouts load from the packaged `resources/` directory
8. Confirm helper binaries are found
9. Confirm icons/resources load
10. Confirm transcode and offline-render flows either work or fail with descriptive app-level errors
11. Confirm audio/backend failures are descriptive and not crashes
12. Confirm the source repo and build folder are not required at runtime

---

## Known Remaining Risks

- Clean-machine manual validation is still pending; CI can verify file presence and dependency resolution, but not full interactive GUI behavior.
- Windows audio backend behavior still needs real-user validation on a non-builder machine.
- If a future dependency becomes dynamically linked on Windows, the package audit must be kept strict so new DLL requirements are staged intentionally.
- A dedicated future pass can re-evaluate static `/MT` runtime linking once the portable ZIP baseline is proven.

---

## Explicit Non-Goals

- NSIS
- MSI / WiX
- winget
- Microsoft Store packaging
- hiding dependency failures behind vague errors
- relying on the source repo or build tree at runtime
