Packaging / Runtime Path Audit
Files / Areas Reviewed
source/gui/imgui/src/App.cpp — resolveProjectPath, findCultTranscoder, findSpatialRenderer, all call sites
source/gui/imgui/src/App.hpp — mRemapPath declaration, mProjectRoot, all path-related members
source/gui/imgui/src/SpatialRootPaths.cpp — defaultCacheRoot, defaultAppSettingsRoot, env var overrides
source/gui/imgui/src/SpatialRootPaths.hpp — public interface
source/gui/imgui/src/main.cpp — argument parsing, --root flag, App construction
source/gui/imgui/CMakeLists.txt — GUI target, install rules (none found)
CMakeLists.txt — top-level, component options, no install rules
source/spatial_engine/spatialRender/main.cpp — resolveCultBinary (existing env-var + app-relative pattern)
source/spatial_engine/realtimeEngine/src/EngineSession.cpp — applyLayout, remapCsvPath handling
internalDocs/AGENTS.md, internalDocs/devHistory.md, internalDocs/REMAP.md
Current Path Model
Before this audit, the GUI had no packaging awareness:

Layout presets: resolved as mProjectRoot / "source/speaker_layouts/...". mProjectRoot defaulted to ".". Works only when run from the repo root or with --root.
cult-transcoder: findCultTranscoder() checked two hardcoded build-tree paths under mProjectRoot. No env var override. No app-relative lookup.
spatialroot_spatial_render: findSpatialRenderer() checked one hardcoded build-tree path. No env var override.
Temp/settings: Already safe — SpatialRootPaths used platform-appropriate directories with SPATIALROOT_TEMP_ROOT and SPATIALROOT_SETTINGS_ROOT overrides.
mRemapPath: Always "" (never set by any GUI control); passed to layout.remapCsvPath = mRemapPath as dead scaffolding.
Proposed Path Model
Three env vars cover packaged builds without requiring app-relative executable introspection or CMake install rules at this stage:

Env var Lookup affected Behavior
SPATIALROOT_ASSET_ROOT Layout preset resolution If set, replaces mProjectRoot for all resolveProjectPath() calls
SPATIALROOT_CULT_TRANSCODER cult-transcoder binary If set and file exists, used directly; if set but missing, surfaces error
SPATIALROOT_SPATIAL_RENDER offline renderer binary Same pattern as CULT
Developer builds: no env vars required. Run from repo root or pass --root. Fallback paths unchanged.

Blocking Issues
None found. The env var overrides are sufficient to make the packaged GUI functional before OS compatibility testing. Full install rules are not needed at this stage.

Non-Blocking Issues
No CMake install rules: No install() calls in any CMakeLists.txt. A distribution pass must define how layouts and helper binaries are staged next to the GUI binary for a proper package.
No app-relative binary lookup: findCultTranscoder() does not use the GUI executable's own path as a lookup anchor (unlike spatialroot_spatial_render, which already has this via resolveCultBinary()). Deferred — env var is sufficient for this phase.
Layout paths assume source/speaker_layouts/ subdirectory structure: SPATIALROOT_ASSET_ROOT must point to the directory containing the full source/speaker_layouts/ subtree. A future packaging task could flatten this to a layouts/ directory next to the binary and update kLayoutPaths[] accordingly.
Layout Preset Findings
Before: resolveProjectPath(kLayoutPaths[i]) resolved as mProjectRoot / "source/speaker_layouts/...". Only worked from repo root.

After: resolveProjectPath() now checks SPATIALROOT_ASSET_ROOT first. If set, all 15 preset paths resolve from that root. The constructor logs the override at startup. Developer behavior is unchanged.

To use in a packaged build: set SPATIALROOT_ASSET_ROOT to the directory containing source/speaker_layouts/ (i.e., the repo root or wherever the layout tree is staged).

CULT Transcoder Findings
Before: findCultTranscoder() checked only two hardcoded build-tree paths. No env var. Error messages did not mention an override mechanism.

After: checks SPATIALROOT_CULT_TRANSCODER env var first. If set and the file exists, returns it immediately. If set but missing, returns "" and the caller surfaces an error. Build-tree fallback is unchanged. All three call-site error messages updated to mention SPATIALROOT_CULT_TRANSCODER.

Note: spatialroot_spatial_render's resolveCultBinary() already checked a CULT_TRANSCODER env var (different name, different tool). The new GUI env var is SPATIALROOT_CULT_TRANSCODER for namespace consistency.

Offline Renderer Findings
Before: findSpatialRenderer() checked one hardcoded build-tree path. No env var.

After: checks SPATIALROOT_SPATIAL_RENDER env var first, consistent with CULT handling. Build-tree fallback unchanged.

Offline render controls remain hidden (kShowOfflineRenderControls = false). The backend wiring is preserved. No production exposure.

Cache / Settings Path Findings
Safe and unchanged. SpatialRootPaths::defaultCacheRoot() and defaultAppSettingsRoot() use platform-correct directories (macOS ~/Library/..., Linux XDG, Windows AppData). Both already support SPATIALROOT_TEMP_ROOT and SPATIALROOT_SETTINGS_ROOT overrides.

mRemapPath Finding
Removed. mRemapPath was always "" — no GUI control ever set it. It was passed as layout.remapCsvPath = mRemapPath but this was always equivalent to leaving the field at its default "". The LayoutInput::remapCsvPath field and the --remap CLI flag remain in EngineSession and spatialroot_realtime for the CSV-based routing path. Only the GUI's dead scaffolding variable was deleted.

CMake / Install Findings
No install rules exist in any CMakeLists.txt. The GUI binary output name is "Spatial Root" (set via OUTPUT_NAME). There are no install(), RPATH, or CPack directives anywhere.

Nothing was added. Adding install rules now would require committing to a directory layout for packaged builds — that decision belongs in the distribution pass. The env var approach is sufficient for OS compatibility testing.

Fixes Made
File Change
App.cpp:1252-1256 resolveProjectPath(): check SPATIALROOT_ASSET_ROOT before mProjectRoot
App.cpp:1258-1275 findCultTranscoder(): check SPATIALROOT_CULT_TRANSCODER env var first
App.cpp:1277-1289 findSpatialRenderer(): check SPATIALROOT_SPATIAL_RENDER env var first
App.cpp constructor Log SPATIALROOT_ASSET_ROOT at startup if set
App.cpp:1043 Removed layout.remapCsvPath = mRemapPath
App.cpp call sites × 3 Updated cult-transcoder "not found" error messages to mention SPATIALROOT_CULT_TRANSCODER
App.hpp:93 Removed std::string mRemapPath member
internalDocs/devHistory.md Added packaging audit entry with env var table and deferred list
internalDocs/AGENTS.md Updated GUI path table to reflect new resolution model
Deferred Risks
CMake install rules: No staging logic. Distribution packaging (DMG, AppImage, installer) requires an install layout to be designed and implemented.
App-relative binary lookup: GUI does not locate binaries relative to its own executable path. Requires platform-specific code (\_NSGetExecutablePath, /proc/self/exe, GetModuleFileName). Not needed until the first true packaged distribution.
Layout directory flattening: kLayoutPaths[] still uses source/speaker_layouts/... relative paths, which works for SPATIALROOT_ASSET_ROOT = /path/to/repo. A real bundle might want a flat layouts/ directory; that requires updating kLayoutPaths[] and the CMake copy step.
macOS .app bundle structure: Not addressed. macOS bundles have Contents/MacOS/, Contents/Resources/ layout. None of this is set up.
spatialroot_spatial_render packaging: Offline render is UNDER CONSTRUCTION. Full packaging of this binary is explicitly deferred.
Documentation Updates
internalDocs/devHistory.md: New entry "Release-Hardening Audit — Packaging / Runtime Path Audit" with full change log, env var table, and deferred list.
internalDocs/AGENTS.md: GUI path table updated from "packaging audit required" to the current resolution model with env var column.
Build / Test Result

./build.sh --gui
Run twice — once after initial changes, once after all changes complete. Both passed:

[100%] Built target spatialroot_gui
✓ Build complete!
spatialroot_realtime, spatialroot_spatial_render, cult-transcoder, spatialroot_gui — all four targets built.
UI smoke test: the GUI binary is at build/source/gui/imgui/Spatial Root. A full launch/path smoke test was not performed (would require display access). The build verification confirms no compilation or link errors were introduced. The path changes are isolated to three small functions and the constructor log.

Recommended Next Audit
OS Compatibility Testing Audit — test the built binaries on macOS, Linux, and (if available) Windows to verify:

GUI launches and layout presets load correctly from the developer repo build
cult-transcoder is found and the Transcode tab functions end-to-end on each platform
Audio device enumeration works on the target OS
File dialogs (NSOpenPanel / zenity / GetOpenFileName) work correctly on each platform
No platform-specific build warnings or runtime errors
This is the natural follow-on now that path resolution is packaging-aware and the codebase has passed all prior audits.
