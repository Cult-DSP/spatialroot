# Agent Task: ADM Direct Streaming (Skip Stem Splitting)

**Status:** ✅ Complete (Implemented & tested 2026-02-24)  
**Priority:** Optimization  
**Scope:** C++ Streaming agent + Python launcher  
**Dependencies:** Phase 4 complete (Spatializer working), `runRealtime.py` parallel pipeline working

> **Implementation Note:** This feature was designed, implemented, and tested in a single session.
> The design below was the plan; the implementation followed **Option A (Shared MultichannelReader)**
> exactly as described. Key files created/modified:
>
> | File                     | Action       | Description                                                                                          |
> | ------------------------ | ------------ | ---------------------------------------------------------------------------------------------------- |
> | `MultichannelReader.hpp` | **Created**  | Shared multichannel WAV reader with de-interleave                                                    |
> | `Streaming.hpp`          | **Modified** | Added `loadSceneFromADM()`, `loaderWorkerMultichannel()`, `initBuffersOnly()`, `parseChannelIndex()` |
> | `RealtimeTypes.hpp`      | **Modified** | Added `std::string admFile` to `RealtimeConfig`                                                      |
> | `main.cpp`               | **Modified** | Added `--adm` CLI flag (mutually exclusive with `--sources`)                                         |
> | `packageForRender.py`    | **Modified** | Added `writeSceneOnly()` function                                                                    |
> | `runRealtime.py`         | **Modified** | ADM path now skips stem splitting, passes `--adm` to engine                                          |
>
> **Test results:** Both paths tested successfully:
>
> - LUSID package (mono files via `--sources`): ✅ No regression
> - ADM WAV (direct streaming via `--adm`): ✅ Full pipeline works end-to-end

---

## Problem

When `runRealtime.py` receives a raw ADM WAV file, it runs the full offline preprocessing pipeline before launching the engine:

```
STEP 1: Verify C++ tools           (~1s, idempotent)
STEP 2: Analyze channels + extract  (~32s for 48ch file — TWO full scans + ADM extract)
STEP 3: Parse ADM → LUSID scene     (~1s)
STEP 4: Package — split stems       (~30-60s, writes N mono WAVs to disk)  ← BOTTLENECK
STEP 5: Launch engine               (instant)
```

**Step 4 is unnecessary for real-time playback.** The stem splitter reads the entire multichannel WAV into memory, extracts each channel, and writes N individual mono WAV files to `processedData/stageForRender/`. The engine then opens those mono files and streams from them.

For SWALE-ATMOS-LFE.wav (48 channels, 111s, 48kHz):

- Reads ~1.9GB interleaved data into Python
- Writes 24 × ~40MB = ~960MB of mono WAVs to disk
- **Total I/O: ~2.9GB of unnecessary disk I/O**

The C++ engine could read directly from the multichannel ADM file, extracting only the channels it needs.

## Proposed Solution

### Overview

Add a **multichannel streaming mode** to `Streaming.hpp` where each `SourceStream` reads from a single shared multichannel WAV file, extracting one channel per source. The Python launcher skips the stem-splitting step and passes the ADM file path + a channel-to-source mapping to the C++ engine.

### Pipeline Comparison

**Current (with stem splitting):**

```
ADM WAV → [Python] analyze channels → extract ADM XML → parse to LUSID → SPLIT STEMS → [C++] open N mono files → stream
```

**Proposed (direct ADM):**

```
ADM WAV → [Python] analyze channels → extract ADM XML → parse to LUSID → write scene.lusid.json only → [C++] open 1 multichannel file → extract channels → stream
```

Steps 1-3 are **unchanged** — we still need ADM metadata extraction and LUSID scene parsing for spatial positions. Only step 4 changes: instead of splitting to mono files, we write just the scene.lusid.json (already done in step 4 anyway) and pass the original ADM path to the engine.

---

## C++ Changes

### 1. `SourceStream` — Add Multichannel Support

Currently `SourceStream::open()` validates `sfInfo.channels == 1` and rejects anything else.

**New behavior:** Accept a multichannel file + a target channel index.

```
Key changes to SourceStream:
- New field: int targetChannel = -1;   // -1 = mono mode, 0+ = extract this channel
- New field: int fileChannels = 1;     // Total channels in the file
- open() accepts optional channelIndex parameter
- If channelIndex >= 0: multichannel mode
  - Don't reject files with channels > 1
  - Store targetChannel and fileChannels
- loadFirstChunk() and loadChunkInto():
  - Read interleaved frames into a temporary buffer
  - Extract targetChannel into the mono playback buffer
  - Temp buffer size = chunkFrames × fileChannels (pre-allocated once)
```

**De-interleaving** on the loader thread (not the audio thread):

```cpp
// In loadChunkInto(), after sf_readf_float into tempBuffer:
if (targetChannel >= 0) {
    // De-interleave: extract one channel from interleaved data
    for (uint64_t i = 0; i < framesRead; ++i) {
        buffer[i] = tempBuffer[i * fileChannels + targetChannel];
    }
} else {
    // Mono mode: direct read (current behavior)
    sf_readf_float(sndFile, buffer.data(), framesToRead);
}
```

**Memory cost:** One temporary interleaved buffer per source that shares the multichannel file. For SWALE (48ch, 240k chunk): `240000 × 48 × 4 bytes = ~44MB` per source. That's too much if duplicated per source.

**Better approach — shared file handle + per-source de-interleave buffer:**

- All sources reading from the same ADM file share a **single** `SNDFILE*` and a **single** interleaved read buffer
- The loader thread reads one chunk of interleaved data, then distributes channels to each source's mono buffer
- This is a significant redesign of how the loader thread works

### 2. Option A: Shared Multichannel Reader (Recommended)

Create a new class `MultichannelReader` that:

1. Opens the multichannel WAV once
2. Holds one interleaved temporary buffer (`chunkFrames × numChannels` floats)
3. Has a channel→SourceStream mapping
4. On each loader cycle, reads one interleaved chunk and distributes channels

```
class MultichannelReader {
    SNDFILE* sndFile;
    SF_INFO sfInfo;
    std::mutex fileMutex;
    std::vector<float> interleavedBuffer;  // chunkFrames × numChannels

    // Map: channel index → SourceStream* (not all channels used)
    std::map<int, SourceStream*> channelMap;

    void readAndDistribute(uint64_t fileFrame) {
        // Read interleaved chunk
        sf_seek(...); sf_readf_float(..., interleavedBuffer.data(), ...);
        // De-interleave into each mapped source's buffer
        for (auto& [ch, stream] : channelMap) {
            for (uint64_t i = 0; i < framesRead; i++) {
                stream->inactiveBuffer()[i] = interleavedBuffer[i * numChannels + ch];
            }
        }
    }
};
```

**Memory: ONE interleaved buffer** = `240000 × 48 × 4 = ~44MB`. Acceptable. The per-source mono buffers remain at `240000 × 4 = ~0.9MB` each (already allocated).

**Disk I/O: ONE sequential read** per chunk, vs N seeks+reads in the current mono approach. Actually **faster** than mono mode for files with many sources — one large sequential read is faster than N small random reads.

### 3. Option B: Simple Per-Source Multichannel (Simpler, Less Optimal)

Each `SourceStream` independently opens the same multichannel file and extracts its channel. Simpler to implement but:

- N file handles to the same file (N = number of active sources)
- N independent seeks per chunk cycle
- N × (chunkFrames × numChannels × 4) bytes of temp buffers — **way too much memory**

**Verdict: Option A is the right approach.**

### 4. Channel Mapping: LUSID Scene → ADM Channel Index

The LUSID scene's source keys are like "11.1", "27.1", "LFE". The ".1" is the hierarchy level. The number before the dot is the **1-based ADM channel number**.

Mapping: `sourceKey "11.1" → ADM channel 11 → 0-based index 10`

For LFE with `_DEV_LFE_HARDCODED = True`: `sourceKey "LFE" → ADM channel 4 → 0-based index 3`

This mapping must be passed from the LUSID scene to the C++ engine. Options:

- **Embed in scene.lusid.json:** Add a `"channelIndex"` field to each source node. Clean but requires LUSID schema change.
- **Derive in C++:** Parse source key "11.1" → extract 11 → subtract 1 → channel index 10. LFE key → hardcoded channel. Simple, no schema change.
- **Separate mapping file:** Write a `channel_map.json`. Overkill.

**Recommended: Derive in C++** — the naming convention is already established and consistent.

### 5. `main.cpp` Changes

New CLI argument: `--adm <path>` — path to the original multichannel ADM WAV file.

When `--adm` is provided:

- Create `MultichannelReader` with the ADM file
- Parse source keys to channel indices
- Load scene into Streaming in multichannel mode
- `--sources` folder is NOT required (scene.lusid.json can live anywhere)

When `--adm` is NOT provided (existing behavior):

- Load from mono files in `--sources` folder (no change)

```
# Current mono mode:
./spatialroot_realtime --layout L --scene S --sources folder/

# New multichannel mode:
./spatialroot_realtime --layout L --scene S --adm sourceData/SWALE-ATMOS-LFE.wav
```

---

## Python Changes

### `runRealtime.py` — `run_realtime_from_ADM()`

Currently:

```python
# Step 4: packageForRender (splits stems + writes scene.lusid.json)
packageForRender(source_adm_file, lusid_scene, contains_audio_data, processed_data_dir)

# Step 5: Launch engine with mono files
_launch_realtime_engine(
    scene_json="processedData/stageForRender/scene.lusid.json",
    sources_folder="processedData/stageForRender",  # mono stems
    ...)
```

After:

```python
# Step 4: Write scene.lusid.json ONLY (no stem splitting)
write_scene_json(lusid_scene, "processedData/stageForRender/scene.lusid.json")

# Step 5: Launch engine with --adm flag pointing to original WAV
_launch_realtime_engine(
    scene_json="processedData/stageForRender/scene.lusid.json",
    adm_file=source_adm_file,  # original multichannel WAV
    ...)
```

Need to extract the "write scene.lusid.json" logic from `packageForRender()` so it can be called independently without stem splitting. Or add a flag to `packageForRender()` to skip stem splitting.

### `_launch_realtime_engine()` — New `adm_file` Parameter

When `adm_file` is provided, use `--adm` instead of `--sources`:

```python
if adm_file:
    cmd += ["--adm", str(Path(adm_file).resolve())]
else:
    cmd += ["--sources", str(sources_path)]
```

---

## Tradeoffs & Considerations

### Benefits

- **~30-60 seconds faster** startup for ADM sources (skip stem splitting)
- **~1-3GB less disk I/O** per launch (no writing/reading mono files)
- **No temp files** cluttering `processedData/stageForRender/`
- **Potentially faster streaming** — one large sequential read vs many small mono reads

### Costs

- **~44MB additional RAM** for the interleaved read buffer (240k frames × 48ch × 4 bytes)
- **New C++ class** (`MultichannelReader`) — moderate complexity
- **Loader thread refactor** — currently iterates sources independently; multichannel mode needs coordinated reads
- **Two code paths** in Streaming (mono mode + multichannel mode) — must maintain both since LUSID packages still use mono files

### Risk Assessment

- **Low risk to existing functionality** — mono mode is untouched, multichannel is additive
- **Medium implementation complexity** — the shared reader + de-interleave is straightforward but touches the core streaming loop
- **Testing** — need to verify identical audio output between mono-stem and direct-ADM paths

### What Still Requires Preprocessing

Even with direct ADM streaming, steps 1-3 are still needed:

1. ✅ C++ tools check (idempotent, ~1s)
2. ✅ Channel analysis (identify empty channels — still needed so the engine knows which channels to skip) + ADM XML extraction (~32s)
3. ✅ Parse ADM XML → LUSID scene (positions/trajectories — essential, ~1s)
4. ❌ ~~Stem splitting~~ — **SKIPPED** (just write scene.lusid.json)
5. ✅ Launch engine

**Net time saved: ~30-60 seconds** depending on file size.

### Future Optimization: Skip Channel Analysis Too?

The channel analysis (step 2's `exportAudioActivity` + `channelHasAudio`) takes ~32 seconds because it scans all 48 channels twice. In the current pipeline, this is used by:

1. `splitStems.py` — to skip empty channels (moot if we skip splitting)
2. `xml_etree_parser.py` — `contains_audio` parameter filters which ADM objects get LUSID nodes

If we skip stem splitting, we could potentially also skip the audio scan and let the C++ engine handle empty channels (it already deals gracefully with silence — DBAP just produces near-zero output). The LUSID parser could include all ADM objects regardless of audio content, and the engine skips channels that are actually silent.

**This would reduce startup to: ADM extract (~2s) + LUSID parse (~1s) + engine launch = ~3 seconds.** But this is a separate optimization to evaluate.

---

## Implementation Plan

### Phase A: Scene-Only Write (Python side)

1. Add `write_scene_only()` function (or flag) to `packageForRender.py`
2. Update `run_realtime_from_ADM()` to call scene-write without stem splitting
3. Update `_launch_realtime_engine()` to accept `adm_file` parameter
4. Test: scene.lusid.json is written correctly without stems

### Phase B: MultichannelReader (C++ side)

1. Create `MultichannelReader` class in new `MultichannelReader.hpp`
2. Add channel extraction from interleaved reads
3. Integrate with `Streaming` class (new `loadSceneFromADM()` method)
4. Add `--adm` argument to `main.cpp`
5. Test: verify audio output matches mono-stem path

### Phase C: Integration Test

1. Run same ADM file through both paths (stem-split vs direct)
2. Compare timing
3. Verify no audible difference

---

## Files to Create/Modify

| File                                                       | Action     | Description                                                   |
| ---------------------------------------------------------- | ---------- | ------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/src/MultichannelReader.hpp` | **Create** | Shared multichannel WAV reader with de-interleave             |
| `spatial_engine/realtimeEngine/src/Streaming.hpp`          | **Modify** | Add `loadSceneFromADM()` method, integrate MultichannelReader |
| `spatial_engine/realtimeEngine/src/main.cpp`               | **Modify** | Add `--adm` CLI arg, dispatch to appropriate loading path     |
| `src/packageADM/packageForRender.py`                       | **Modify** | Add option to write scene.lusid.json without splitting stems  |
| `runRealtime.py`                                           | **Modify** | Skip stem splitting, pass `--adm` to engine                   |
