# New behavior

Startup: App reads default_layout.json from the platform settings directory. If valid, sets mLayoutPath to that path and logs success. If missing, continues silently. If present but invalid/unreadable, logs a non-fatal amber warning and continues without a layout.
"Set as Default" button: reads current mLayoutPath, validates the JSON, atomically copies to settings dir, writes metadata. Appears disabled when no layout is selected.
"Clear Default" button: removes default_layout.json and .meta.json; never touches the original user file. Appears disabled when no default is saved.
Status indicator: shows one of none saved / loaded (name, saved-at) / saved file invalid / unavailable.

# Follow-up recommendations

Tests: The SPATIALROOT_SETTINGS_ROOT env override makes DefaultLayoutManager fully testable in a temp dir without touching the real settings dir — a CTest unit covering save/load/clear/invalid-JSON would be straightforward to add.
Windows testing: The %APPDATA% path branch hasn't been exercised on a real Windows build yet.
Schema version in metadata: the task spec mentions schemaVersion and appVersion fields — these can be added to the metadata write in saveDefaultLayout() once those values are surfaced from the engine or build system.
