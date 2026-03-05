(2026-03-04) LUSID package selection fixes and debug logging

- Added debug logging to the realtime GUI input panel to trace source selection and detection:
	- `RealtimeInputPanel._browse_source`: prints selected path when the user picks a file or directory.
	- `RealtimeInputPanel._on_source_changed`: prints detection hint and flags when the source text changes.
	- `RealtimeInputPanel._detect_source`: prints existence, is_file/is_dir, and whether `scene.lusid.json` was found.

- macOS file-dialog behavior workaround:
	- On macOS (`sys.platform == "darwin"`) prefer `QFileDialog.getExistingDirectory()` first so users can select a LUSID package folder (the native file dialog may otherwise only allow file selection).
	- Keep the original fallback behavior (try file then directory) on other platforms.

- Small housekeeping:
	- Added an import of `sys` to support platform detection.

- Manual checks performed and results:
	- Verified the LUSID package folder at `sourceData/LUSID_package` is visible from the runtime and contains `scene.lusid.json`.
	- Verified the default speaker layout `spatial_engine/speaker_layouts/allosphere_layout.json` exists.
	- After the changes, debug prints will appear in the terminal running `python realtimeMain.py` to show why a selected source is accepted or rejected.

Next steps / notes:

- If the GUI still rejects a folder on macOS, copy the `[DEBUG]` lines from the terminal and paste them into the issue report — they show the exact path and detection flags.
- Consider removing or gating the debug prints behind a verbose/debug flag once the issue is resolved.

