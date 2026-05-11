# Packaging / Runtime Path Audit

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

# Gui transcoder audit

Transcoder GUI Bug Pass
Files / Areas Reviewed
source/gui/imgui/src/App.cpp
source/gui/imgui/src/App.hpp
source/gui/imgui/src/SpatialRootPaths.cpp
source/gui/imgui/src/SpatialRootPaths.hpp
source/gui/imgui/src/main.cpp
source/gui/imgui/src/SubprocessRunner.cpp
internalDocs/AGENTS.md
internalDocs/devHistory.md
internal/cult_transcoder/README.md
internal/cult_transcoder/internalDocs/AGENTS-CULT.md
internal/cult_transcoder/src/main.cpp
internal/cult_transcoder/src/packaging/adm_package.cpp
Bugs Found
Transcoder success state was exit-code-only, so the GUI could report success even if expected output files were missing.
Workflow switching only reset mTcDone/mTcSuccess; stale log text and stale status context could linger.
Workflow controls stayed editable while a transcode was running, which risked confusing post-run state.
Command preview text did not match actual args in all cases because it omitted --report paths and did not show quoting for spaced paths.
Workflow 1 used permissive file-or-directory pickers where type-specific pickers were needed.
Button gating only checked for non-empty strings, not real file/dir validity or transcoder-binary availability.
Invalid SPATIALROOT_CULT_TRANSCODER was only surfaced as generic “not found”.
Workflow 0 package mode built its GUI report path inside the destination package directory, which was brittle.
The CLI smoke test exposed a remaining non-GUI issue: package-adm-wav currently fails on data/sourceData/CANYON-ATMOS-LFE.wav with Failed while writing package stems.
Fixes Made
source/gui/imgui/src/App.cpp
Added exact command-preview construction from the same arg vectors passed to SubprocessRunner.
Added shell-style quoting in command preview/log display so spaced paths are shown accurately.
Disabled workflow/mode editing while a transcode is active.
Cleared stale transcode log/status on workflow or mode changes and before new runs.
Tightened button gating to require valid existing inputs, valid output path shape, and a resolvable cult-transcoder.
Added inline error text for missing/invalid SPATIALROOT_CULT_TRANSCODER.
Switched Workflow 1 browse actions to type-correct pickers.
Changed manual-transcode completion logic to verify expected output file existence before reporting success.
Logged full executed command lines instead of abbreviated placeholders.
Moved Workflow 0 package-mode GUI report path to a sibling \*\_report.json path instead of inside the package directory.
source/gui/imgui/src/App.hpp
Added GUI-only state for transcode status detail and expected-output verification.
internalDocs/devHistory.md
Added a release-hardening entry for this Transcoder GUI bug pass.
internalDocs/AGENTS.md
Updated the troubleshooting table with the new env-var guidance and the observed package-adm-wav backend limitation.
Workflow 0: ADM/BW64 to LUSID
Current behavior:
Scene JSON mode now requires an existing input file, a valid output file path, and a found transcoder binary.
Full package mode now requires an existing input file, a package-directory-style output path, and a found transcoder binary.
Both modes now show command previews including --report, --stdout-report, and quoting.
Bugs fixed:
Stale state/log reset.
Preview/actual-args mismatch.
Weak button gating.
Generic transcoder-not-found messaging.
Success-with-missing-output false positive.
Package report path generation moved out of the package directory.
Remaining risks:
package-adm-wav itself still failed in CLI smoke testing on CANYON-ATMOS-LFE.wav with Failed while writing package stems. That looks like a backend/CULT issue, not a GUI-state issue.
Workflow 1: LUSID to ADM/BW64
Current behavior:
Mode 0 now expects a real scene.lusid.json file plus a real WAV directory.
Mode 1 now expects a real LUSID package directory.
Output XML/WAV paths must be file paths, not directories.
Preview now includes --report and reflects the real arg list.
Bugs fixed:
Wrong browse control types.
Weak input/output validation.
Preview/actual-args mismatch.
Success-with-missing-output false positive.
Stale run/log state.
Remaining risks:
I did not expose or redesign experimental adm-author options; they remain backend-wired but not expanded here.
No GUI launch/manual tab test was run in this pass because I did not have display access.
Command Preview vs Actual Args
After this pass, the preview strings are built from the same argument vectors used for mTcRunner.start(...).
That includes --report, --stdout-report, --lfe-mode, and shell-style quoting for paths with spaces.
I verified this path by running CLI smoke tests with outputs under /private/tmp/spatialroot tc smoke/....
Success / Failure Detection
Manual Transcoder-tab runs now require:
subprocess exit code 0, and
expected primary output(s) to exist.
If exit code is nonzero, the GUI reports failure.
If exit code is 0 but expected output is missing, the GUI now reports failure and logs the missing path.
Missing report files are logged as warnings when the main outputs exist.
stdout and stderr remain merged and surfaced through the shared transcode log via SubprocessRunner.
Path Handling
SPATIALROOT_CULT_TRANSCODER still works as the first lookup path.
If SPATIALROOT_CULT_TRANSCODER is set to a missing path, the GUI now shows a clear inline message instead of only a generic not-found error.
Developer build-tree fallback still works through findCultTranscoder().
Paths with spaces were exercised in CLI smoke tests and are now shown correctly in preview/log text.
Workflow 0 Scene mode uses an output file path.
Workflow 0 Package mode uses a package directory path plus a sibling report file path.
Workflow 1 uses separate output file paths for XML and WAV.
Button Gating / State Reset
Verified/fixed:
Run buttons are disabled while a transcode is already running.
Run buttons now require valid existing inputs and valid output-path shape.
Workflow switching clears stale status/log context.
Output-type switching clears incompatible stale mTcOutput values.
Workflow 1 mode switching clears stale status without unnecessarily erasing user-entered paths.
Remaining Known Limitations
package-adm-wav failed in CLI smoke testing on data/sourceData/CANYON-ATMOS-LFE.wav with package-adm-wav: Failed while writing package stems. I did not attempt a backend fix in this GUI-only pass.
I did not perform a live GUI/manual smoke test because display access was not available.
I did not add packaging/install logic, app-relative lookup, or backend refactors.
Documentation Updates
internalDocs/devHistory.md
Added Release-Hardening Audit — Transcoder GUI Bug Pass (May 11, 2026).
internalDocs/AGENTS.md
Updated ADM/transcoder troubleshooting notes for SPATIALROOT_CULT_TRANSCODER and the observed package-mode backend failure.
Build / Test Result
Ran ./build.sh --gui three times during the pass; final build passed.
Ran CLI smoke test:
build/internal/cult_transcoder/cult-transcoder transcode --in data/sourceData/CANYON-ATMOS-LFE.wav --in-format adm_wav --out '/private/tmp/spatialroot tc smoke/canyon scene.lusid.json' --out-format lusid_json --report '/private/tmp/spatialroot tc smoke/canyon scene_report.json' --stdout-report --lfe-mode hardcoded
Passed.
