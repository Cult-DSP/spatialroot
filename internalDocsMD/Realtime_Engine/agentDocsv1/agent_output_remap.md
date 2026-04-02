# Output Remap Agent (CSV-based, minimal)

> **Implementation Status: ✅ COMPLETE (Phase 7, Feb 25 2026)**
> Implemented in `spatial_engine/realtimeEngine/src/OutputRemap.hpp`.
> See `realtime_master.md` Phase 7 Completion Log for details.
> CSV format: `layout,device` (0-based, `#` comments, case-insensitive headers).
> Identity fast-path detected automatically — zero overhead when no CSV supplied.
> `mRemap` pointer set once before `start()`, const/read-only during playback.
> CLI flag: `--remap <path>` in `main.cpp`.

> **Purpose**: remap the engine's internal **layout channel order** to the **physical device channel order** at the very end of the audio callback.
> Keep this **simple**: load a CSV once, build a mapping table, then apply the exact same “copy/accumulate” loop we already use in the AlloApp remap.

---

## What changes vs the old doc

The previous version over-scoped this agent (downmix matrices, format conversion, interleaving, etc.). That can live elsewhere if needed.
This agent should do **one thing**:

- **Map internal channel index (“layout”) → device output channel index (“device”)**
- Apply that map in the existing “copy `mRenderIO` → `AudioIO`” loop.

(Everything else is out-of-scope for this phase.)

---

## Remap CSV format

The remap file is a **CSV with headers**:

| column   | meaning                                                                                |
| -------- | -------------------------------------------------------------------------------------- |
| `layout` | internal channel index (0-based) in the engine’s render buffer (the “layout” ordering) |
| `device` | output channel index (0-based) in the physical device / `AudioIO`                      |

Optional extra columns are allowed and ignored (so we can keep notes in the CSV).

### Example

```csv
layout,device
0,0
1,1
2,2
3,3
```

### Notes

- Indices are **0-based**.
- It is valid for multiple `layout` rows to map to the same `device` (we **sum/accumulate** into that device channel).
- If a `layout` index is out of range, ignore it (and log once during load, not in RT).
- If a `device` index is out of range, ignore it (and log once during load, not in RT).

---

## Data model (what to build at load time)

Build a small struct that is RT-safe and immutable during playback:

```cpp
struct RemapEntry { int layout; int device; };

struct OutputRemapTable {
  std::vector<RemapEntry> entries;   // compact list for “accumulate” mode
  int maxDeviceIndex = -1;           // for quick sanity checks / debug
  bool identity = true;              // fast path if CSV not provided or equals identity
};
```

Why “entries list” instead of a big matrix:

- It matches the AlloApp approach: **iterate rows, copy/accumulate**.
- It’s minimal and avoids per-frame branching.

---

## Load behavior

### Where the CSV comes from

- A path provided in config/flags (whatever mechanism the engine already uses for file paths).
- If missing: **identity mapping**.

### Parsing rules

- Parse once on startup (or during explicit reconfigure, never inside the audio callback).
- Accept headers case-insensitively: `layout`, `device`.
- Trim whitespace.
- Skip empty lines and comment lines that start with `#` (optional, nice-to-have).

### Validation

- Remove duplicates of the exact same `(layout, device)` pair.
- Mark `identity=true` only if the mapping is exactly `layout==device` for all channels and covers the active range.

---

## Runtime application (audio thread)

### Insertion point

Use the existing end-of-block copy loop that currently does identity mapping from `mRenderIO` → `AudioIO`.
Replace it with:

- Clear device outputs (or ensure the destination buffer is already zeroed).
- For each `RemapEntry`, **accumulate** from internal layout channel → device channel.

Pseudo-code:

```cpp
// frames = io.framesPerBuffer()
// mRenderIO: float** [layoutChannels][frames]
// io.outBuffer(): float** or equivalent [deviceChannels][frames] (planar)

if (remap.identity) {
  // existing fast path: direct copy for min(layoutCh, deviceCh)
} else {
  // ensure device outputs are zero (if required by backend)
  for (auto e : remap.entries) {
    float* dst = io.outBuffer(e.device);
    const float* src = mRenderIO[e.layout];
    for (int i = 0; i < frames; ++i) dst[i] += src[i];
  }
}
```

### Real-time constraints

- No allocations.
- No file IO.
- No logging per callback.
- The remap table is read-only during playback.

---

## Edge cases (keep predictable)

- **Missing CSV**: identity mapping.
- **CSV maps nothing valid**: output silence (or fall back to identity if you prefer; choose one and document it).
- **device channel not written**: should remain silence.
- **multiple layout → same device**: sums (accumulate), because that is the simplest and matches typical routing behavior.

---

## Testing (quick + practical)

1. Make a “channel ID” test render where each internal channel outputs a different sine frequency or noise burst.
2. Create a remap CSV that swaps a couple channels.
3. Verify the swap on the physical outputs (or in a loopback recording).
4. Add one CSV row that maps two layout channels to one device channel; confirm summing.

---

## Documentation update rule

When this agent is implemented:

- Update `RENDERING.md` with:
  - the CSV schema (`layout,device`)
  - the runtime behavior (accumulate remap)
  - where to configure the CSV path (flags/config)
