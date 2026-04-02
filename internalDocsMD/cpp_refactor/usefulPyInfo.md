# Useful Python Source Information — Pre-Deletion Reference

This document records information extracted from the Python source files before their removal. It covers only items that are NOT already captured in `EngineSession.hpp`, `RealtimeTypes.hpp`, or `PUBLIC_DOCS/API.md`.

---

## 1. OSC Parameter Addresses (complete list)

Source: `gui/realtimeGUI/realtime_panels/RealtimeControlsPanel.py`

The following OSC addresses are sent by the Python GUI to `al::ParameterServer` on `127.0.0.1:9009`. These are the canonical address strings the C++ engine must register.

| OSC Address                    | Type    | Range / Values            | Default | Notes                                          |
|--------------------------------|---------|---------------------------|---------|------------------------------------------------|
| `/realtime/gain`               | `float` | 0.1 – 3.0                 | 0.5     | Master output gain (linear)                    |
| `/realtime/focus`              | `float` | 0.2 – 5.0                 | 1.5     | DBAP rolloff exponent                          |
| `/realtime/speaker_mix_db`     | `float` | -10.0 – +10.0             | 0.0     | Loudspeaker mix trim in dB                     |
| `/realtime/sub_mix_db`         | `float` | -10.0 – +10.0             | 0.0     | Subwoofer mix trim in dB                       |
| `/realtime/auto_comp`          | `float` | 0.0 (off) or 1.0 (on)     | 0.0     | Focus auto-compensation; sent as float         |
| `/realtime/elevation_mode`     | `float` | 0.0 / 1.0 / 2.0           | 0.0     | Cast to int: 0=RescaleAtmosUp, 1=RescaleFullSphere, 2=Clamp |
| `/realtime/paused`             | `float` | 0.0 (play) or 1.0 (pause) | —       | Transport pause/resume; sent as float          |

**Why this matters:** The ImGui GUI must register OSC parameters using exactly these address strings for compatibility with any existing control surfaces or test scripts that send to port 9009.

**Note on auto_comp and paused:** Both boolean-intent parameters are sent as `float` (0.0/1.0) because `python-osc SimpleUDPClient.send_message()` sends all values as OSC floats. The C++ ParameterServer must accept float for these addresses.

---

## 2. GUI Default Parameter Values

Source: `gui/realtimeGUI/realtime_panels/RealtimeControlsPanel.py` (DEFAULTS dict)

These are the GUI-side defaults, explicitly confirmed to match the C++ engine startup defaults:

```
gain:           0.5
focus:          1.5
speaker_mix_db: 0.0
sub_mix_db:     0.0
auto_comp:      False (0.0)
elevation_mode: 0  (RescaleAtmosUp)
```

**Note on divergence:** `RealtimeTypes.hpp` sets `dbapFocus{1.0f}` as the atomic default, but `RuntimeParams` in `EngineSession.hpp` sets `dbapFocus = 1.5f`. The GUI default of 1.5 matches `RuntimeParams` and is the value passed at launch. The atomic default of 1.0 is the pre-configure state that is overwritten by `configureRuntime()`. This distinction matters if a future GUI initialises the engine without calling `configureRuntime()` first.

---

## 3. `spatialroot_realtime` CLI Argument Names and Defaults

Source: `runRealtime.py` (`_launch_realtime_engine` and `_build_args`)

The C++ binary is invoked with these named flags. The ImGui GUI's `build.sh` / CMake layer or any direct CLI usage must use these exact names:

```
--layout   <path>          Speaker layout JSON (required)
--scene    <path>          LUSID scene JSON (required)
--samplerate <int>         Default: 48000
--buffersize <int>         Default: 512; UI options: 64/128/256/512/1024
--gain     <float>         Default: 0.5; validated range [0.1, 3.0]
--focus    <float>         Default: 1.5; validated range [0.2, 5.0]
--osc_port <int>           Default: 9009
--adm      <path>          ADM WAV for direct streaming (mutually exclusive with --sources)
--sources  <path>          Folder of mono WAV files (mutually exclusive with --adm)
--remap    <path>          Optional channel remap CSV
--device   <string>        Exact output device name (optional; omit for system default)
```

**Validation performed by the Python launcher (replicate in ImGui launcher):**
- `gain` must be in `[0.1, 3.0]`; rejects outside that range.
- `dbap_focus` must be in `[0.2, 5.0]`; rejects outside that range.
- Exactly one of `--adm` or `--sources` must be provided; both or neither is an error.
- `scene_path` and `layout_path` must exist on disk before launch.
- `remap_csv` path must exist if provided.

---

## 4. Source Type Detection Logic

Source: `runRealtime.py` (`checkSourceType`), `runPipeline.py` (`checkSourceType`), `gui/realtimeGUI/realtime_panels/RealtimeInputPanel.py` (`_detect_source`)

The heuristic for determining input type is:

1. If path does not exist → error.
2. If path is a **file** and ends with `.wav` (case-insensitive) → **ADM** source.
3. If path is a **directory** and contains `scene.lusid.json` directly inside it → **LUSID package**.
4. Otherwise → unrecognised / error.

**Key design decision:** The LUSID check is by `scene.lusid.json` presence inside the directory, NOT by the directory name. This was an explicit improvement noted in the code comments: "works for any package directory name."

The GUI also detects ADM XML and LUSID JSON for the Transcode panel, using these additional rules (source: `RealtimeTranscodePanel._detect_input`):
- File ending `.xml` → ADM XML format.
- File ending `.lusid.json` or `.json` → LUSID JSON.
- Directory containing any `*.lusid.json` files → LUSID package directory.

---

## 5. `cult-transcoder` CLI Invocation Details

Source: `runRealtime.py`, `runPipeline.py`, `gui/realtimeGUI/realtime_transcoder_runner.py`

**Full command for ADM → LUSID conversion:**
```
cult_transcoder/build/cult-transcoder transcode \
    --in         <adm_wav_path> \
    --in-format  adm_wav \
    --out        processedData/stageForRender/scene.lusid.json \
    --out-format lusid_json \
    [--report    <report_json_path>] \
    [--lfe-mode  hardcoded|speaker-label]
```

**Binary locations:**
- macOS/Linux: `cult_transcoder/build/cult-transcoder`
- Windows: `cult_transcoder/build/Release/cult-transcoder.exe` (Visual Studio multi-config) or `cult_transcoder/build/cult-transcoder.exe` (Ninja single-config); check both.

**Output file convention (used by GUI auto-derive):**
- If output path not specified: derive as `<input_stem>.lusid.json` in the same directory as input.
- Report path: `<output_stem>_report.json` alongside the LUSID output (strip `.lusid` suffix from stem before appending `_report`).
- cult-transcoder also writes a debug XML artifact to `processedData/currentMetaData.xml`.
- cult-transcoder writes `scene.lusid.json` atomically: writes to `<out>.tmp` then renames to final path.

**LFE mode options:**
- `hardcoded` — channel index 4 (1-indexed) is always treated as LFE. This was the default and matches `_DEV_LFE_HARDCODED = True` in the old `splitStems.py`.
- `speaker-label` — LFE detection from ADM speaker label (alternative mode).

**Common failure causes documented in code (useful for error handling in ImGui):**
- WAV file has no axml chunk (not an ADM BW64 file).
- ADM XML failed to parse (malformed metadata).
- Output path not writable (check `processedData/stageForRender/` exists).
- Binary not found at expected path (not built yet).

**Output directories that must pre-exist (created by Python before invocation):**
```
processedData/
processedData/stageForRender/
```

---

## 6. Build System Details Not in build.sh

Source: `src/config/configCPP_posix.py`, `src/config/configCPP_windows.py`

**Initialization flag:** The project uses a `.init_complete` file at repo root as a guard to check whether `init.sh` has been run. All Python launchers check for this file. The C++ GUI should check for it too, or replicate the relevant checks (submodules present, binaries built).

**Submodule initialization check:** Idempotency is achieved by checking for `thirdparty/allolib/include` before running `git submodule update`. If that directory exists, submodules are considered initialized.

**cult-transcoder submodule:** Has its own nested `thirdparty/libbw64` submodule inside `cult_transcoder/`. Must be initialized separately by running `git submodule update --init --depth 1 thirdparty/libbw64` from within the `cult_transcoder/` directory (not from repo root). CMakeLists.txt issues `FATAL_ERROR` if `bw64.hpp` is missing at configure time.

**CMake flag used for spatial renderer and realtime engine builds:**
```
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 <source_path>
```
This flag is set in `runCmake()` (posix and windows) for the spatial renderer and realtime engine. The cult-transcoder build does NOT use this flag.

**Windows build difference:** Windows uses `cmake --build . --parallel N --config Release` for all targets (multi-config generator support). macOS/Linux uses `make -jN` for spatial renderer and realtime engine, but `cmake --build` for cult-transcoder. A cleanup note in the code recommends migrating all posix builds to `cmake --build` for consistency.

**Executable name check on Windows:** On Windows, check both `build/Release/cult-transcoder.exe` (Visual Studio) and `build/cult-transcoder.exe` (Ninja) for idempotency — both paths are valid depending on the generator used.

---

## 7. Engine Launch Startup Sequence (GUI → Engine)

Source: `gui/realtimeGUI/realtime_runner.py`

The Python GUI used a specific sentinel string to detect when the C++ engine's ParameterServer was ready to receive OSC:

```python
_ENGINE_READY_SENTINEL = "ParameterServer listening"
```

**Launch sequence:**
1. GUI transitions to `LAUNCHING` state and starts the process.
2. GUI waits up to 3000 ms for the process to start (`waitForStarted`).
3. GUI scans each stdout line for `"ParameterServer listening"`.
4. On finding that sentinel, GUI transitions to `RUNNING` and fires `engine_ready`.
5. On `engine_ready`, GUI calls `flush_to_osc()` — sends all current control values to the engine immediately, so any values set during LAUNCHING are applied.

**Why flush_to_osc on engine_ready matters:** The C++ ParameterServer isn't bound until after engine startup, so OSC sends during LAUNCHING are silently dropped. Flushing on engine_ready ensures the engine's first audio block uses the user's intended values rather than engine defaults.

**Restart bug workaround (important for ImGui GUI):** On Restart, slider state must be reset to defaults BEFORE restarting the engine. Without this, `flush_to_osc()` on `engine_ready` pushes whatever stale values were left from the previous run (e.g. `gain=1.5` from a slider move) into the new engine instance. This caused above-unity gain on every restart. The fix: call `reset_to_defaults()` before `restart()`, not after.

**Graceful stop sequence:** SIGTERM → wait 3000 ms → SIGKILL.

**OSC debounce:** Slider `valueChanged` signals are debounced at 40 ms (collapse rapid moves into a single send). Checkbox / combobox / spinbox changes are sent immediately (no debounce). The ImGui GUI should replicate this behaviour or use a similar quiet-period strategy for sliders.

**Exit code handling:** Exit codes 0, -2, and 130 are all treated as clean exits (0 = normal, -2 / 130 = SIGINT / Ctrl+C).

---

## 8. Remap CSV Format

Source: `runRealtime.py` (`_launch_realtime_engine` docstring)

```
Format: 'layout,device' columns, 0-based indices, header row required.
```

Example file shipped with the repo: `spatial_engine/remaping/exampleRemap.csv`

---

## 9. Speaker Layout Presets Known to the GUI

Source: `gui/realtimeGUI/realtime_panels/RealtimeInputPanel.py`

The dropdown offered two presets:
- `AlloSphere` → `spatial_engine/speaker_layouts/allosphere_layout.json` (default)
- `Translab` → `spatial_engine/speaker_layouts/translab-sono-layout.json`

The ImGui GUI should offer the same presets or populate from the `speaker_layouts/` directory.

---

## 10. processedData Directory Structure

Source: `runRealtime.py`, `gui/realtimeGUI/realtime_runner.py`, `gui/realtimeGUI/realtime_transcoder_runner.py`

These directories are created before any subprocess launch:
```
processedData/                   — output root for all pipeline artifacts
processedData/stageForRender/    — cult-transcoder writes scene.lusid.json here
```

`scene.lusid.json` generated from an ADM WAV is always written to `processedData/stageForRender/scene.lusid.json` by the Python launcher. The C++ binary then reads it from that path when launched in ADM mode.

---

## 11. Per-Channel Audio Activity Scan (Retired Feature)

Source: `src/analyzeADM/checkAudioChannels.py`, `runRealtime.py` removed-imports block

This feature (`--scan_audio` flag, `exportAudioActivity()`, `containsAudio.json`) was explicitly removed in Phase 3 (2026-03-04) and is superseded by cult-transcoder, which assumes all channels active. The following details are retained in case the scan is ever needed for diagnostic tooling:

**Algorithm:** Chunked per-channel RMS scan. Scans 30 chunks per channel (each `chunk_size=48000` samples), with a skip factor to distribute chunks across the full file duration. Threshold: `-100 dBFS`. Early-exits per channel once threshold is exceeded.

**Output:** `processedData/containsAudio.json` — JSON with per-channel `{channel_index, rms_db, contains_audio}` entries.

**Performance note:** Adds approximately 14 seconds of startup time for a typical ADM file (noted in removed CLI help text). This was the primary motivation for removing it.

---

## 12. Stem Splitting Conventions (splitStems.py — Offline Pipeline Only)

Source: `src/packageADM/splitStems.py`

This is used only by the offline `runPipeline.py` path, not the realtime engine. Documented here in case the LUSID package format is ever regenerated from an ADM source without cult-transcoder.

**Mono WAV naming convention (LUSID v0.5):**
- Standard channels: `{chanNumber}.1.wav` (1-indexed, where `chanNumber` is the 1-based ADM channel index)
- LFE: `LFE.wav`
- LFE detection: Hardcoded as channel 4 (`_DEV_LFE_HARDCODED = True`). The `speaker-label` mode was not implemented in Python; it is only available in cult-transcoder.

**Output directory:** `processedData/stageForRender/` — existing WAV files in this directory are deleted before splitting.

**Empty channel handling:** Channels with `contains_audio=False` in `containsAudio.json` are skipped; channel numbering is preserved (gaps are possible in the mono file set).

---

## 13. RF64 / Large File Awareness in analyzeRender.py

Source: `src/analyzeRender.py`

The offline render analysis tool includes a sanity check for WAV files that exceeded the 4 GB limit before RF64 was implemented:

> If `file_size_bytes > expected_data_bytes + 1_000_000`, the header-reported sample count is likely wrong (32-bit data-chunk size wrap). The tool warns and estimates actual duration from file size.

This check is not needed in the realtime engine but is relevant if render analysis tooling is ever rebuilt for the C++ pipeline.

**dB analysis method:** 1-second RMS windows, floor at -120 dB for silence, range plotted as -120 to 0 dB. Batches of 10 channels per subplot, saved as PDF.

---

## 14. Example Files (getExamples.py)

Source: `utils/getExamples.py`

Three ADM WAV example files are available on Google Drive. These are the canonical test sources for the pipeline:

| Filename                  | Google Drive File ID              |
|---------------------------|-----------------------------------|
| `driveExample1.wav`       | `16Z73gODkZzCWjYy313FZc6ScG-CCXL4h` |
| `driveExample2.wav`       | `1-oh0tixJV3C-odKdcM7Ak-ziCv5bNKJB` |
| `driveExampleSpruce.wav`  | `1NsW8xj4wFGhGtSKRIuPL4E2yoEJEIq4X` |

Download URL pattern: `https://drive.google.com/uc?id=<file_id>` (via `gdown`). Files are saved to `sourceData/`.

`driveExampleSpruce.wav` is the default test file used in `runPipeline.py` and `runRealtime.py` inline examples.

---

## 15. macOS File Dialog Workaround

Source: `gui/widgets/input_panel.py` (`_choose_file`)

macOS native file dialogs do not allow selecting both files and directories in the same dialog. The workaround used in Python:

- On macOS: show directory picker first; if cancelled, show file picker.
- On other platforms: show file picker first; if cancelled, show directory picker.

This is relevant for any ImGui file-picker that needs to accept both ADM WAV files and LUSID package directories. Dear ImGui's `ImGui::OpenFileDialog` or platform-native wrappers will need equivalent logic.

---

## 16. Offline Pipeline Step Count and Step Detection

Source: `gui/pipeline_runner.py`

The offline pipeline (runPipeline.py) has 7 steps. The GUI inferred step progress from stdout using:
- Regex `STEP N` (for numbered steps)
- Keyword phrases mapped to step numbers:
  - "Verifying C++ tools" → step 1
  - "Extracting ADM metadata" → step 2
  - "Channel activity" → step 3
  - "Parsing ADM → LUSID" → step 4
  - "Packaging audio" → step 5
  - "Running … renderer" → step 6
  - "Analyzing …" → step 7
- Regex `N%` for percentage progress

This is relevant if a progress display is added to the ImGui GUI for any multi-step subprocess.
