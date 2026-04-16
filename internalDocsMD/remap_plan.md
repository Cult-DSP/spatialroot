Routing Architecture Refactor — Layout-Derived Output Routing (Revised)

---

## Summary Sentence

The engine renders to a compact internal bus and routes to a layout-defined output bus; the layout JSON is the only supported public routing source.

---

## 1. Terminology

### 1.1 Engine Naming Policy

| Concept | Canonical engine term | Notes |
|---|---|---|
| Compact, contiguous, DBAP-owned render bus | **internal bus** / **internal channels** | Never "render channels" in engine-facing code |
| Physical AudioIO output bus, layout-derived width | **output bus** / **output channels** | Never "device channels" in engine-facing code |
| Layout JSON field | **deviceChannel** | Keep this name — it is a schema field, not an engine concept |
| Bus width: internal side | **internalChannelCount** | Local variable name; accessor: `numInternalChannels()` |
| Bus width: output side | **outputChannelCount** | Local variable name; `mConfig.outputChannels` is the config store |
| Routing table | **output routing table** / `mOutputRouting` | Never "auto-remap" or "remap feature" |
| Output routing copy: identity layout | **identity copy** | renderChannels == outputChannels, all entries diagonal |
| Output routing copy: non-identity layout | **scatter routing** | Internal channels scattered into non-contiguous output slots |
| Subwoofer index: internal space | `mSubwooferInternalChannels` | Replaces `mSubwooferChannels` / `mSubwooferRenderChannels` |
| Subwoofer index: output space | `mSubwooferOutputChannels` | Replaces `mSubwooferDeviceChannels` |
| Internal-space classifier | `isInternalSubwooferChannel(int ch)` | Replaces `isSubwooferChannel` / `isSubwooferRenderChannel` |
| Output-space classifier | `isOutputSubwooferChannel(int ch)` | Replaces `isSubwooferDeviceChannel` |

### 1.2 What This Changes From the Previous Plan

The previous plan used "render/device" terminology as intermediate names.  
This revision adopts "internal/output" consistently throughout engine code, comments, logs, and accessors. The word "device" survives only when directly referencing the layout JSON field `deviceChannel`.

`numRenderChannels()` is renamed to `numInternalChannels()`. There is no strong compatibility reason to defer this — it is only called from `EngineSession.cpp` (one site) and the plan's own logging strings.

---

## 2. Two-Space Model

### Internal channel space

- Width: `internalChannelCount = numSpeakers + numSubwoofers`
- Channels `0..numSpeakers-1` — DBAP output per speaker
- Channels `numSpeakers..internalChannelCount-1` — LFE per subwoofer
- Compact, contiguous, 0-based, no gaps
- `mRenderIO.channelsOut()` always equals `internalChannelCount`
- All DBAP math, LFE routing, mix trims, NaN guard, and Phase 14 diagnostics operate in this space

### Output channel space

- Width: `outputChannelCount = max(all layout .deviceChannel values) + 1`
- Sparse — gaps are valid and intentional (e.g. Allosphere starts at channel 1)
- `mConfig.outputChannels` holds this value; it is written by `Spatializer::init()` and read by `RealtimeBackend` to open AudioIO
- No internal DSP code reads `mConfig.outputChannels` as internal bus width

### Routing table (the only bridge)

- Entries: `{internalChannel, outputChannel}` pairs derived from the layout JSON
- `mOutputRouting` (type: `OutputRemap`) owned by `Spatializer`
- Built once during `init()`, immutable during playback
- `mRemap` non-owning pointer set to `&mOutputRouting` after `init()`

---

## 3. Final Invariants

After `init()` returns `true`:

1. `mRenderIO.channelsOut() == numSpeakers + numSubwoofers` — always compact, never sparse.
2. `mConfig.outputChannels == max(all .deviceChannel) + 1` — output bus width only.
3. `mRemap != nullptr` — always set to `&mOutputRouting`.
4. `mSubwooferInternalChannels[j] == numSpeakers + j` for all `j` — contiguous by construction.
5. `isInternalSubwooferChannel(ch)` is only called with `ch < internalChannelCount`.
6. `isOutputSubwooferChannel(ch)` is only called with `ch < outputChannelCount`.
7. No internal DSP code reads `mConfig.outputChannels` as internal-bus width (enforced by audit in §6).
8. `checkIdentity()` returns `true` only when `outputChannelCount == internalChannelCount` AND all entries are diagonal AND full coverage holds — no ambiguity.
9. All DBAP math, proximity guard, onset fade, fast-mover sub-stepping are untouched.
10. `init()` returns `false`, logs a descriptive error, and refuses to start on any validation failure.
11. Every call to `buildAuto()` after a successful validation gate produces exactly `internalChannelCount` routing entries. Fewer entries after validation is a hard internal error, not a silent drop.

---

## 4. Validation Gate

Located at the top of `Spatializer::init()`, before routing construction. Returns `false` with a descriptive logged error on any of:

| Check | Error message |
|---|---|
| `layout.speakers.empty()` | "Speaker layout has no speakers." |
| Any `speaker.deviceChannel < 0` | "Speaker N has negative deviceChannel." |
| Any `subwoofer.deviceChannel < 0` | "Subwoofer N has negative deviceChannel." |
| Duplicate `deviceChannel` across speakers | "Duplicate speaker deviceChannel K." |
| Duplicate `deviceChannel` across subwoofers | "Duplicate subwoofer deviceChannel K." |
| Speaker `deviceChannel == subwoofer deviceChannel` | "Speaker and subwoofer share deviceChannel K." |

**Additional warning (non-fatal):**
- Any `deviceChannel` value > 127: emit `[Spatializer] WARNING: deviceChannel K on speaker/subwoofer N is suspiciously large — possible layout typo.` This threshold is advisory; it does not block startup. The number 127 is chosen as well above any real hardware channel count without being so tight as to reject unusual but valid sparse layouts.

**Empty routing table after construction:**
- After `buildAuto()` returns, if `mOutputRouting.entries().empty()` and the layout passed validation, this is a hard internal error. Log `[Spatializer] INTERNAL ERROR: routing table is empty after successful validation` and return `false`. This state is impossible by construction, but must be guarded explicitly.

**Implementation note:** build a `std::set<int>` across all speaker `deviceChannel` values first, then check each subwoofer against it. O(N log N), acceptable at init time.

---

## 5. Phase 7: Routing Stage — Explicit Behavior

### 5.1 Identity copy (fast path)

**Condition:** `mRemap->identity() == true`  
This is valid only when `outputChannelCount == internalChannelCount` AND all routing entries are diagonal (`entry.internalChannel == entry.outputChannel`) AND full bijective coverage holds.

**Behavior:** Direct contiguous copy, internal channel N → output channel N, for N in `0..internalChannelCount-1`. No clearing required — the backend already zeroes `io.outBuffer()` before `renderBlock()` is called.

**No additional output-bus clearing needed:** since `outputChannelCount == internalChannelCount`, no output channels are left unmapped.

### 5.2 Scatter routing (non-identity path)

**Condition:** `mRemap->identity() == false`

**Behavior — mandatory two-step:**

**Step A — Clear the full output bus before scatter:**  
Before scattering, all `outputChannelCount` channels in `io.outBuffer()` must contain only zeros. The backend zeroes `io.outBuffer()` before `renderBlock()` is called, so this is already satisfied by existing backend behavior. This guarantee **must be explicitly documented as a contract between the backend and `renderBlock()`** — not assumed. If the backend contract ever changes, the scatter path must clear the output bus itself before scattering.

The revised plan must add an explicit comment at the Phase 7 scatter block:
```
// PRECONDITION: all io.outBuffer(ch) for ch in 0..outputChannelCount-1
// are zeroed by the backend before renderBlock() is called.
// Unmapped output channels remain silent by this guarantee.
// If this precondition ever changes, add an explicit zero-fill here.
```

**Step B — Scatter:** For each routing entry `{internalCh, outputCh}`, accumulate `mRenderIO.outBuffer(internalCh)` into `io.outBuffer(outputCh)`.

**Unmapped output channels:** Any output channel index not present as an `outputCh` in the routing table receives no samples and remains at zero from the backend's pre-clear. This is correct and intentional — it must never be left to chance.

**The current Phase 7 implementation has a latent risk in the identity path:** `std::min(renderChannels, numOutputChannels)` is used as the copy limit. After this patch, `renderChannels` (internal) and `numOutputChannels` (output) are different variables. The identity path is only entered when `outputChannelCount == internalChannelCount`, so `min()` should produce the same value — but the code must explicitly use `internalChannelCount` on the internal side and `outputChannelCount` on the output side. Do not carry the `std::min()` guard silently into the new code; make both widths explicit.

### 5.3 Summary table

| Layout type | Path | Output bus cleared | Unmapped channels |
|---|---|---|---|
| Identity (translab) | Fast copy | By backend pre-zero (no gaps exist) | None — all output channels mapped |
| Non-identity (allosphere) | Scatter routing | By backend pre-zero, confirmed by precondition comment | Remain zero from pre-zero |

---

## 6. `mConfig.outputChannels` Audit

**Required before patching.** Grep every read of `mConfig.outputChannels` and classify each site.

### Known sites (from current grep):

| File | Line | Site classification | Action required |
|---|---|---|---|
| `EngineSession.cpp:133` | Log: "Output channels (from layout)" | Output-space — correct | Update log string to say "output channels" |
| `EngineSession.cpp:158` | `mOutputRemap->load(mRemapCsv, mConfig.outputChannels, mConfig.outputChannels)` | **Stale/wrong — passes outputChannels as both renderChannels and deviceChannels** | Fix: pass `mSpatializer->numInternalChannels()` as first arg, `mConfig.outputChannels` as second |
| `RealtimeBackend.hpp:97` | Log: "Output channels:" | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:153` | `devMaxOut < mConfig.outputChannels` | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:158` | Error log | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:168` | Error log | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:182` | Opens AudioIO with this channel count | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:207` | Comment | Output-space / backend — correct | Update comment to "output channels" terminology |
| `RealtimeBackend.hpp:212` | Validation: `actualOutChannels < mConfig.outputChannels` | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:215` | Error log | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:226` | `actualOutChannels > mConfig.outputChannels` | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:228` | Log | Output-space / backend — correct | No change needed |
| `Spatializer.hpp:234` (current) | Writes the computed value | Write site — this is the assignment | Will be replaced by the patch |
| `RealtimeTypes.hpp:161` | Comment | Documentation — update | Update comment |

**The only stale/wrong read is `EngineSession.cpp:158`** — it passes `mConfig.outputChannels` as the internal-bus width (`renderChannels` arg to `load()`). This must be fixed to `mSpatializer->numInternalChannels()`.

**Required grep sweep before coding:** Run `grep -rn "outputChannels"` across the full `spatial_engine/` and `gui/` trees to confirm no additional read sites exist outside those listed. The above list is derived from the current codebase state but must be verified as exhaustive.

---

## 7. `buildAuto()` Failure Policy

### Decision: hard-fail assertion after validation

The previous plan said "out-of-range entries are dropped" in `buildAuto()`. This is wrong after the hard validation gate runs.

**Rationale:** If the validation gate passed, every `deviceChannel` value in the layout is non-negative and non-duplicate. The `buildAuto()` loop constructs entries from those exact values, using `internalChannelCount` and `outputChannelCount` computed from those same values. An out-of-range entry at this point is not a user error — it is an internal engine bug: either the routing table construction is wrong, or the bus width computation is inconsistent with the entry generation.

**Required behavior in `buildAuto()`:**
- Do not silently drop entries.
- If any constructed entry has `internalCh < 0 || internalCh >= internalChannelCount`, log `[OutputRouting] INTERNAL ERROR: constructed entry has out-of-range internal channel %d (max %d)` and either assert (debug builds) or return without populating the table (which triggers the empty-table guard in `init()`).
- If any constructed entry has `outputCh < 0 || outputCh >= outputChannelCount`, same treatment.
- In both cases, this is a programming error, not a runtime condition.

**Contrast with `load()` (CSV path):** The CSV `load()` method may still silently drop out-of-range entries from user-provided CSV files, because those are genuinely untrusted external inputs. The policy difference is intentional.

---

## 8. File-by-File Patch Plan

### `spatial_engine/realtimeEngine/src/Spatializer.hpp`

**`init()` — lines 201–309 (current subwoofer + channel count + routing block)**

Replace:
```cpp
mSubwooferChannels.push_back(sub.deviceChannel);
int maxChannel = mNumSpeakers - 1;
for (int subCh : mSubwooferChannels) { if (subCh > maxChannel) maxChannel = subCh; }
int computedOutputChannels = maxChannel + 1;
mConfig.outputChannels = computedOutputChannels;
mRenderIO.channelsOut(computedOutputChannels);
mFastMoverScratch.channelsOut(computedOutputChannels);
```

With:
```cpp
// ── Validation gate ──────────────────────────────────────────────────────
// [see §4 above — returns false with descriptive error on any violation]

// ── Internal space: subwoofer internal channel indices ──────────────────
// LFE sources occupy internal channels numSpeakers..numSpeakers+numSubs-1.
mSubwooferInternalChannels.clear();
mSubwooferOutputChannels.clear();
for (int j = 0; j < numSubs; ++j) {
    mSubwooferInternalChannels.push_back(mNumSpeakers + j);
    mSubwooferOutputChannels.push_back(layout.subwoofers[j].deviceChannel);
}

// ── Compute internal and output bus widths ───────────────────────────────
const int internalChannelCount = mNumSpeakers + numSubs;

int maxOutputCh = 0;
for (const auto& spk : layout.speakers)
    maxOutputCh = std::max(maxOutputCh, spk.deviceChannel);
for (int oc : mSubwooferOutputChannels)
    maxOutputCh = std::max(maxOutputCh, oc);
const int outputChannelCount = maxOutputCh + 1;

// outputChannelCount → config for the backend to open AudioIO.
// internalChannelCount → internal only, never stored in mConfig.
mConfig.outputChannels = outputChannelCount;

mRenderIO.channelsOut(internalChannelCount);
mFastMoverScratch.channelsOut(internalChannelCount);

// ── Build layout-derived output routing table ────────────────────────────
// Speaker i   (internal=i)               → output=layout.speakers[i].deviceChannel
// Sub j       (internal=numSpeakers+j)   → output=layout.subwoofers[j].deviceChannel
{
    std::vector<RemapEntry> entries;
    entries.reserve(internalChannelCount);
    for (int i = 0; i < mNumSpeakers; ++i)
        entries.push_back({i, layout.speakers[i].deviceChannel});
    for (int j = 0; j < numSubs; ++j)
        entries.push_back({mNumSpeakers + j, layout.subwoofers[j].deviceChannel});
    mOutputRouting.buildAuto(std::move(entries), internalChannelCount, outputChannelCount);
}
// Guard: routing table must be non-empty after successful validation.
if (mOutputRouting.entries().empty()) {
    std::cerr << "[Spatializer] INTERNAL ERROR: routing table is empty after successful validation." << std::endl;
    return false;
}
mRemap = &mOutputRouting;
```

**`renderBlock()` — LFE routing loop (lines 396–404)**

Change `mSubwooferChannels` → `mSubwooferInternalChannels`. The loop writes to `mRenderIO.outBuffer(subCh)` where `subCh` is an internal channel index — correct by construction.

**`renderBlock()` — Phase 6 mix trim (lines 668–686)**

Change all `isSubwooferChannel(ch)` → `isInternalSubwooferChannel(ch)`.  
These iterate `ch = 0..internalChannels-1` (internal space).

**`renderBlock()` — Phase 14 internal-bus diagnostic (lines 747–828)**

Change all `isSubwooferChannel(ch)` → `isInternalSubwooferChannel(ch)`.  
These iterate internal space. (4 call-sites.)

**`renderBlock()` — Phase 14 output-bus diagnostic (lines 869–944)**

Change all `isSubwooferChannel(ch)` → `isOutputSubwooferChannel(ch)`.  
These iterate `ch = 0..outputChannelCount-1` (output space). (3 call-sites.)

**`renderBlock()` — Phase 7 copy block (lines 830–867)**

The identity path currently uses `std::min(renderChannels, numOutputChannels)` as the copy limit. After this patch, identity is only entered when `outputChannelCount == internalChannelCount`, so they are equal. However, make both widths explicit in the code rather than relying on `std::min()` to hide any discrepancy:
```cpp
// Identity path: direct copy, internal ch N → output ch N.
// Valid only because checkIdentity() guarantees outputChannelCount == internalChannelCount.
for (unsigned int ch = 0; ch < renderChannels; ++ch) {
    const float* src = mRenderIO.outBuffer(ch);
    float* dst = io.outBuffer(ch);
    for (unsigned int f = 0; f < numFrames; ++f)
        dst[f] += src[f];
}
```
```cpp
// Scatter routing path:
// PRECONDITION: all io.outBuffer(ch) for ch in 0..outputChannelCount-1
// are zeroed by the backend before renderBlock() is called.
// Unmapped output channels remain silent by this guarantee.
// If this precondition ever changes, add an explicit zero-fill here.
for (const auto& entry : mRemap->entries()) {
    // These bounds checks are last-resort guards; they should never fire
    // after a successful validation + buildAuto(). If they do, it is a bug.
    if (static_cast<unsigned int>(entry.layout) >= renderChannels) continue;
    if (static_cast<unsigned int>(entry.device) >= numOutputChannels) continue;
    const float* src = mRenderIO.outBuffer(entry.layout);
    float* dst = io.outBuffer(entry.device);
    for (unsigned int f = 0; f < numFrames; ++f)
        dst[f] += src[f];
}
```

**Accessor rename:**

```cpp
// Renamed from numRenderChannels()
unsigned int numInternalChannels() const {
    return static_cast<unsigned int>(mRenderIO.channelsOut());
}
```

**`isSubwooferChannel()` — remove and replace (line 1049):**

Remove `isSubwooferChannel()`.

Add:
```cpp
// Internal-space helper: ch is in 0..internalChannelCount-1
bool isInternalSubwooferChannel(int ch) const {
    for (int ic : mSubwooferInternalChannels)
        if (ic == ch) return true;
    return false;
}
// Output-space helper: ch is in 0..outputChannelCount-1
bool isOutputSubwooferChannel(int ch) const {
    for (int oc : mSubwooferOutputChannels)
        if (oc == ch) return true;
    return false;
}
```

**Private member changes (lines 1119–1157):**

| Old | New | Note |
|---|---|---|
| `std::vector<int> mSubwooferChannels` | `std::vector<int> mSubwooferInternalChannels` | Internal indices: numSpeakers, numSpeakers+1, … |
| *(new)* | `std::vector<int> mSubwooferOutputChannels` | Physical output channels from layout `.deviceChannel` |
| `const OutputRemap* mRemap = nullptr` | keep | Non-owning pointer, initialized to `&mOutputRouting` in `init()` |
| *(new)* | `OutputRemap mOutputRouting` | Owned routing table |

**Doc comment updates:**

- Class header (lines 12–16): replace old formula with two-space model description using internal/output terminology.
- `init()` comment block: replace "outputChannels formula" with internal/output split.
- Threading READ-ONLY list (lines 62–66): update `mSubwooferChannels` → `mSubwooferInternalChannels`, `mSubwooferOutputChannels`.
- Internal render buffer comment (lines 277–298): remove stale "future Channel Remap agent" language; state that the routing is now active.

---

### `spatial_engine/realtimeEngine/src/OutputRemap.hpp`

**Add `buildAuto()` after `load()` (after line 179):**

```cpp
// Build the output routing table from layout-derived entries (no CSV).
// Called once by Spatializer::init() on the main thread, before start().
// internalChannels: width of the internal bus (numSpeakers + numSubwoofers).
// outputChannels:   width of the physical output bus (max deviceChannel + 1).
// Post-validation: all entries must be in range. Out-of-range entries are
// a hard internal error, not a silent drop.
void buildAuto(std::vector<RemapEntry> entries,
               int internalChannels,
               int outputChannels) {
    mEntries.clear();
    mMaxDeviceIndex = -1;
    for (auto& e : entries) {
        if (e.layout < 0 || e.layout >= internalChannels) {
            std::cerr << "[OutputRouting] INTERNAL ERROR: out-of-range internal channel "
                      << e.layout << " (max " << internalChannels - 1 << ")" << std::endl;
            mEntries.clear();
            return;
        }
        if (e.device < 0 || e.device >= outputChannels) {
            std::cerr << "[OutputRouting] INTERNAL ERROR: out-of-range output channel "
                      << e.device << " (max " << outputChannels - 1 << ")" << std::endl;
            mEntries.clear();
            return;
        }
        mEntries.push_back(e);
        if (e.device > mMaxDeviceIndex) mMaxDeviceIndex = e.device;
    }
    mIdentity = checkIdentity(internalChannels, outputChannels);
    std::cout << "[OutputRouting] " << mEntries.size()
              << " layout-derived routing entries"
              << (mIdentity ? " — identity copy active" : " — scatter routing active")
              << std::endl;
}
```

**`checkIdentity()` — update signature and guard (line 211):**

```cpp
bool checkIdentity(int internalChannels, int outputChannels) const {
    // Identity requires internal and output bus widths to be equal.
    // Any gap in output channel numbering means output > internal,
    // which makes scatter routing necessary.
    if (outputChannels != internalChannels) return false;
    if (static_cast<int>(mEntries.size()) != internalChannels) return false;
    std::vector<bool> covered(internalChannels, false);
    for (const auto& e : mEntries) {
        if (e.layout != e.device) return false;
        if (covered[e.layout])   return false; // duplicate
        covered[e.layout] = true;
    }
    for (bool c : covered) if (!c) return false;
    return true;
}
```

**Header comment block (lines 1–40):** Replace all "render/device" language with internal/output. Remove references to CSV as the primary routing mechanism. State that `buildAuto()` is the standard path and `load()` is the legacy CSV path.

**`RemapEntry` field names:** The fields are currently `layout` (internal) and `device` (output). These are used by both `buildAuto()` and the legacy `load()` path. Do not rename them in this patch — the struct is used internally only and the field names are not user-visible. Add a comment clarifying: "`layout` = internal channel index; `device` = output channel index."

---

### `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`

**`RealtimeConfig::outputChannels` comment (lines 157–166):**

```cpp
// Physical output bus width — derived from the speaker layout's deviceChannel
// values: outputChannelCount = max(all .deviceChannel values) + 1.
// Set by Spatializer::init() and read by RealtimeBackend::init() to open
// AudioIO with the correct physical channel count.
// This is NOT the internal bus width. Internal bus width (numSpeakers +
// numSubwoofers) is owned by Spatializer and never stored in RealtimeConfig.
int outputChannels = 0;
```

---

### `spatial_engine/realtimeEngine/src/EngineSession.cpp`

**`configureRuntime()` — remap block (lines 155–165):**

Fix the stale bug: `mConfig.outputChannels` is currently passed as both `renderChannels` and `deviceChannels` to `load()`. After this patch:

```cpp
mOutputRemap = std::make_unique<OutputRemap>();
if (!mRemapCsv.empty()) {
    // LEGACY: CSV remap is deprecated. Layout-derived output routing is now standard.
    // This path remains temporarily as internal scaffolding during validation only.
    std::cerr << "[EngineSession] WARNING: --remap CSV is deprecated. "
              << "Physical output routing is now derived from the speaker layout JSON. "
              << "CSV support will be removed after validation." << std::endl;
    bool remapOk = mOutputRemap->load(mRemapCsv,
        mSpatializer->numInternalChannels(),  // internal bus width
        mConfig.outputChannels);              // output bus width
    if (remapOk) {
        mSpatializer->setRemap(mOutputRemap.get());
    } else {
        std::cout << "[EngineSession] CSV load failed — retaining layout-derived routing." << std::endl;
    }
} else {
    std::cout << "[EngineSession] Layout-derived output routing active ("
              << mSpatializer->numInternalChannels() << " internal → "
              << mConfig.outputChannels << " output channels)." << std::endl;
    // mRemap is already set to &mOutputRouting by Spatializer::init().
}
```

---

### `spatial_engine/realtimeEngine/src/EngineSession.hpp`

**`LayoutInput::remapCsvPath` deprecation comment:**

```cpp
// DEPRECATED: physical output routing is now derived from layout deviceChannel values.
// This field is retained temporarily as internal scaffolding only.
// Not a supported user workflow. Will be removed after layout-routing validation.
std::string remapCsvPath;
```

---

### `spatial_engine/realtimeEngine/src/main.cpp`

**`--remap` flag (line 159):**

```cpp
// DEPRECATED: CSV remap path. Not a supported user workflow.
// Retained temporarily as internal scaffolding. Will be removed.
layoutIn.remapCsvPath = getArgString(argc, argv, "--remap");
```

---

### `gui/imgui/src/App.cpp` and `App.hpp`

**Current plan** keeps the CSV field visible with a `TextDisabled` deprecation label. This is insufficient for the product intent.

**Revised treatment:** Move the CSV field into a collapsible "Legacy / Internal" section that is visually hidden by default:

```cpp
if (ImGui::CollapsingHeader("Legacy / Internal (not a supported workflow)")) {
    ImGui::TextDisabled("REMAP CSV — deprecated. Output routing is now layout-derived.");
    // ... existing Browse button, kept functional for internal validation only
}
```

This makes the CSV path visually non-primary without removing it from the build. Users who need it for validation can expand the section; users following normal workflow will never see it.

**`App.hpp:78`:**

```cpp
std::string mRemapPath; // DEPRECATED — remove after layout-routing validation
```

---

## 9. Remaining Single-Bus Assumptions — Audit List

The following are likely sites of residual single-bus assumptions outside the already-identified list. These must be verified by grep before coding begins.

1. **`mFastMoverScratch`** — sized to `computedOutputChannels` in the current code. After the patch it must be sized to `internalChannelCount`. Verify: search for all uses of `mFastMoverScratch.channelsOut(` and confirm only one write site exists.

2. **Phase 14 loop bounds** — the device-bus diagnostic loop (lines 869–944) iterates `ch = 0..numOutputChannels-1`. Verify that `numOutputChannels` is taken from `io.channelsOut()` (the real AudioIO buffer, which equals `outputChannelCount`) and not from `mRenderIO.channelsOut()` (which equals `internalChannelCount`).

3. **`spatialRender/` offline renderer** — `spatialroot_spatial_render` has its own channel calculation logic. Verify it does not share `Spatializer.hpp` and that its own channel model is unaffected by this patch.

4. **AlloLib `al::AudioIO` framesPerBuffer / channelsOut interaction** — confirm that calling `mRenderIO.channelsOut(internalChannelCount)` followed by `io.channelsOut(outputChannelCount)` for the real AudioIO device produces independent buffer allocations with no aliasing.

5. **Any log strings that say "render channels"** — sweep with `grep -rn "render channel"` and replace with "internal channels" in all non-schema, non-history text.

6. **`numRenderChannels()` call sites** — there is at least one in `EngineSession.cpp:188` (current plan). After renaming to `numInternalChannels()`, confirm there are no other callers via grep.

---

## 10. CSV Deprecation and Removal Path

### Immediate (this patch)

- GUI: CSV field demoted to collapsed "Legacy / Internal" section, hidden by default.
- CLI `--remap`: adds deprecation comment; behavior unchanged.
- `EngineSession.cpp`: adds `[WARNING]` to stderr when `--remap` is used.
- `EngineSession.hpp`: `remapCsvPath` gets deprecation comment.
- No removal yet.

### After validation (next step, not this patch)

When layout-derived routing has been verified against all target layouts (translab + allosphere), in a separate commit:

1. Remove `--remap` flag from `main.cpp`.
2. Remove `remapCsvPath` from `EngineSession.hpp` and `LayoutInput`.
3. Remove the CSV branch from `EngineSession.cpp::configureRuntime()`.
4. Remove the CSV UI block from `App.cpp` entirely.
5. Remove `mRemapPath` from `App.hpp`.
6. The `OutputRemap::load()` method may remain in `OutputRemap.hpp` for reference or future tooling use, but it is no longer called from any engine path.

---

## 11. Risks and Test Plan

### Risks

| Risk | Mitigation |
|---|---|
| Internal/output bus size mismatch passed to mRenderIO vs mFastMoverScratch | Audit all `channelsOut()` write sites; add invariant check in `init()` that `mRenderIO.channelsOut() == internalChannelCount` after init |
| Backend pre-zero guarantee for scatter path not actually upheld | Add explicit comment documenting precondition; grep `RealtimeBackend` for io buffer zeroing behavior before coding |
| `numRenderChannels()` rename breaks a call site not yet found | Grep exhaustively before renaming; rename is safe to defer to last if uncertain |
| Phase 14 device-bus diagnostic uses wrong loop bound after split | Verify `numOutputChannels` source in that block is `io.channelsOut()` not `mRenderIO.channelsOut()` |
| `EngineSession.cpp:158` stale bug (outputChannels as both args) is copied into the new path | Explicitly listed as required fix in §6; cannot be missed |

### Test Plan

**Build verification:**
- `./build.sh --engine-only` — fix any compile errors from renamed members before proceeding.

**Identity layout (translab):**
- Consecutive `deviceChannel` values `0..17`, subwoofer at `18` or consecutive.
- Expected: log shows `identity copy active`, `mOutputRouting.identity() == true`.
- Verify audio output is correct and nanGuardCount stays 0.

**Sparse layout (allosphere):**
- Speakers starting at `deviceChannel 1` (or with gaps in the sequence).
- Expected: log shows `scatter routing active`, routing entries listed, device channels match layout JSON.
- Verify audio routes to correct physical outputs.
- Verify unmapped output channels (e.g. channel 0 if allosphere starts at 1) produce no audio.

**Leading gap test:**
- Layout where lowest speaker `deviceChannel` is > 0 (e.g. all channels start at `deviceChannel 4`).
- Expected: output channels `0..3` are silent; speaker audio appears at channels `4+`.

**Middle gap test:**
- Layout where `deviceChannel` values are non-consecutive (e.g. 0, 1, 3, 5 — missing 2 and 4).
- Expected: output channels 2 and 4 are silent; speakers at 0, 1, 3, 5 receive audio.

**Trailing gap test:**
- Layout where `outputChannelCount` is larger than the highest speaker index (e.g. speakers at 0–7, subwoofer at 16, outputChannelCount = 17).
- Expected: output channels 8–15 are silent.

**Validation failures:**
- Layout with duplicate speaker `deviceChannel` → hard fail, engine refuses to start, descriptive error logged.
- Layout with speaker and subwoofer sharing `deviceChannel` → same.
- Layout with negative `deviceChannel` → same.
- Layout with no speakers → same.

**Legacy CSV path (internal validation only):**
- Run with `--remap` flag: confirm `[WARNING]` in stderr, CSV still applied correctly with `numInternalChannels()` as the internal-bus argument (not `outputChannels`).

**Phase 14 diagnostic stability:**
- After routing patch, confirm `renderDomMask` and `deviceDomMask` remain stable (no spurious relocation events).
- In identity case, both masks should be equal.
- In scatter case, masks should reflect the output-channel layout.
