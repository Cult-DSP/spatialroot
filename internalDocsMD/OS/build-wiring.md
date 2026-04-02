# OS & Build Wiring — C++ Tool Configuration Notes

**Created:** March 7, 2026  
**Author:** Lucian Parisi  
**Status:** Active — notes for future dev

---

## Overview

This document captures known inconsistencies in how the C++ tools in spatialroot are built across
platforms, and lays out the recommended migration path for future developers.

---

## Current State (as of 2026-03-07)

### POSIX (`configCPP_posix.py`)

| Tool                         | Build command   | Notes                                      |
| ---------------------------- | --------------- | ------------------------------------------ |
| `cult-transcoder`            | `cmake --build` | Correct. Added 2026-03-07.                 |
| `spatialroot_spatial_render` | `make -jN`      | Works, but generator-specific (Unix Make). |
| `spatialroot_realtime`       | `make -jN`      | Works, but generator-specific (Unix Make). |

### Windows (`configCPP_windows.py`)

| Tool                         | Build command                    | Notes    |
| ---------------------------- | -------------------------------- | -------- |
| `cult-transcoder`            | `cmake --build --config Release` | Correct. |
| `spatialroot_spatial_render` | `cmake --build --config Release` | Correct. |
| `spatialroot_realtime`       | `cmake --build --config Release` | Correct. |

---

## The Inconsistency

`buildSpatialRenderer()` and `buildRealtimeEngine()` on POSIX call `runCmake()`, which issues:

```bash
cmake <source>        # configure
make -jN              # build
```

`buildCultTranscoder()` on POSIX (and all Windows builds) use:

```bash
cmake -B <build> -DCMAKE_BUILD_TYPE=Release <source>   # configure
cmake --build <build> --parallel N                      # build
```

The `cmake --build` form is **generator-independent** — it works with Unix Makefiles, Ninja, and
Visual Studio generators without modification. The raw `make` call will silently fail if the
project was configured with a Ninja generator, or on Windows.

---

## Why It Was Left As-Is

The existing `buildSpatialRenderer()` / `buildRealtimeEngine()` calls have been stable and working
on POSIX. Changing `runCmake()` risks breaking already-working builds without a test environment
on both platforms. The `cult-transcoder` build was new as of 2026-03-07, so it was written
correctly from the start using `cmake --build`.

---

## Recommended Future Migration

When revisiting the posix build functions:

1. Replace `runCmake()` internals — swap out the `make -jN` call:

   ```python
   # OLD
   result = subprocess.run(["make", f"-j{num_cores}"], cwd=str(build_path), ...)

   # NEW
   result = subprocess.run(
       ["cmake", "--build", str(build_path), "--parallel", str(num_cores)],
       cwd=str(project_root), ...
   )
   ```

2. Update the cmake configure call in `runCmake()` to use `-B`:

   ```python
   # OLD
   result = subprocess.run(["cmake", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5", str(source_path)],
                           cwd=str(build_path), ...)

   # NEW
   result = subprocess.run(
       ["cmake", "-B", str(build_path), "-DCMAKE_BUILD_TYPE=Release", str(source_path)],
       cwd=str(project_root), ...
   )
   ```

3. Remove the `runCmake()` function once both callers are migrated.

4. Confirm `buildSpatialRenderer()` and `buildRealtimeEngine()` still work on macOS and Linux
   after the change (run `init.sh` from a clean tree without existing build dirs).

---

## cult-transcoder Submodule Nesting

cult_transcoder is a git submodule of spatialroot, **and** it has its own nested submodule
(`thirdparty/libbw64`). From spatialroot's perspective, the nested submodule is tracked in:

```
.git/modules/cult_transcoder/modules/thirdparty/libbw64
```

To initialize it from the spatialroot root:

```bash
git submodule update --init cult_transcoder
cd cult_transcoder
git submodule update --init thirdparty/libbw64
```

`initializeCultTranscoderSubmodules()` in both config files handles this automatically by
running `git submodule update --init --depth 1 thirdparty/libbw64` from inside the
`cult_transcoder/` directory.

The presence check uses:

```
cult_transcoder/thirdparty/libbw64/include/bw64/bw64.hpp
```

which is the exact path that `cult_transcoder/CMakeLists.txt` checks in its FATAL_ERROR guard.

---

## FetchContent Dependencies (cult-transcoder)

These are fetched at cmake configure time — no manual install needed:

| Dependency | Version | Source                                 |
| ---------- | ------- | -------------------------------------- |
| Catch2     | v3.5.3  | https://github.com/catchorg/Catch2.git |
| pugixml    | v1.14   | https://github.com/zeux/pugixml.git    |

Because FetchContent clones into `cult_transcoder/build/_deps/`, a fresh configure on a machine
with no network access will fail. This is expected behaviour — the machine must have internet
access the first time `init.sh` is run.

---

## References

- `src/config/configCPP_posix.py` — POSIX build functions
- `src/config/configCPP_windows.py` — Windows build functions
- `cult_transcoder/CMakeLists.txt` — cult-transcoder build system
- `internalDocsMD/AGENTS.md §OS-Specific C++ Tool Configuration` — canonical summary
- `cult_transcoder/internalDocsMD/AGENTS-CULT.md §8, §9` — cult-transcoder submodule + binary path contracts
