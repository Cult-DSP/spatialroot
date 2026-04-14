Routing Architecture Refactor — Layout-Derived Output Routing
Context
The engine currently conflates two distinct channel spaces into a single integer (mConfig.outputChannels): the internal render bus (compact, 0-based, DBAP-owned) and the physical device output bus (sparse, layout-defined). This conflation means the render buffer is sized and indexed by device channel numbers, making non-consecutive layouts (Allosphere) require a manual CSV remap to produce correct output. The layout JSON already contains all necessary routing information in speakers[*].deviceChannel and subwoofers[*].deviceChannel. This patch makes those fields the sole authoritative source of physical output routing, eliminates the render/device conflation, adds a hard-fail validation gate before routing construction, and deprecates the CSV path immediately as a user-facing feature. The CSV mechanism may survive temporarily as internal scaffolding but must not be presented as a supported workflow.

Two Channel Spaces — Definitions
Render space (renderChannelCount = numSpeakers + numSubwoofers):

Channels 0..numSpeakers-1 → DBAP output per speaker
Channels numSpeakers..renderChannelCount-1 → LFE per subwoofer
Compact, contiguous, owned by mRenderIO
Width exposed by Spatializer::numRenderChannels()
Device space (deviceChannelCount = max(all .deviceChannel values) + 1):

Width of the physical AudioIO output bus
Stored in mConfig.outputChannels (device-facing only)
mRenderIO.channelsOut() must never equal mConfig.outputChannels in non-identity layouts
All internal DSP paths operate in render space. The routing table (OutputRemap) is the only bridge between the two spaces.

Validation Gate — Hard Fail Before Routing Construction
Located at the top of Spatializer::init(), before any routing construction. Returns false with a descriptive error on any of:

layout.speakers.empty() — "Speaker layout has no speakers."
Any speaker.deviceChannel < 0 — "Speaker N has negative deviceChannel."
Any subwoofer.deviceChannel < 0 — "Subwoofer N has negative deviceChannel."
Duplicate deviceChannel across all speakers — "Duplicate speaker deviceChannel K."
Duplicate deviceChannel across all subwoofers — "Duplicate subwoofer deviceChannel K."
Any speaker deviceChannel == any subwoofer deviceChannel — "Speaker and subwoofer share deviceChannel K."
Implementation: build a std::set<int> across all speakers first, then check each subwoofer against it. O(N log N), acceptable at init time.

File Changes
spatial_engine/realtimeEngine/src/Spatializer.hpp
init() — rewrite the subwoofer + channel count + routing construction block (currently lines 201–309)

Replace:

mSubwooferChannels.push_back(sub.deviceChannel); // device value
int maxChannel = mNumSpeakers - 1;
for (int subCh : mSubwooferChannels) { if (subCh > maxChannel) maxChannel = subCh; }
int computedOutputChannels = maxChannel + 1;
mConfig.outputChannels = computedOutputChannels;
mRenderIO.channelsOut(computedOutputChannels);
mFastMoverScratch.channelsOut(computedOutputChannels);
With:

// ── Validation gate ──────────────────────────────────────────────
// [validation logic here — see Validation Gate section above]

// ── Render space: subwoofer render indices ───────────────────────
// LFE sources route to consecutive render channels numSpeakers..N+M-1.
mSubwooferRenderChannels.clear();
mSubwooferDeviceChannels.clear();
for (int j = 0; j < numSubs; ++j) {
mSubwooferRenderChannels.push_back(mNumSpeakers + j);
mSubwooferDeviceChannels.push_back(layout.subwoofers[j].deviceChannel);
}

// ── Compute render and device bus widths ─────────────────────────
const int renderChannelCount = mNumSpeakers + numSubs;

int maxDeviceCh = 0;
for (const auto& spk : layout.speakers) maxDeviceCh = std::max(maxDeviceCh, spk.deviceChannel);
for (int dc : mSubwooferDeviceChannels) maxDeviceCh = std::max(maxDeviceCh, dc);
const int deviceChannelCount = maxDeviceCh + 1;

// Device bus width is written into config for the backend to open AudioIO.
// Render bus width is internal to Spatializer only.
mConfig.outputChannels = deviceChannelCount;

mRenderIO.channelsOut(renderChannelCount);
mFastMoverScratch.channelsOut(renderChannelCount);

// ── Build layout-derived routing table ───────────────────────────
// Speaker i (render=i) → device=layout.speakers[i].deviceChannel
// Sub j (render=numSpeakers+j) → device=layout.subwoofers[j].deviceChannel
{
std::vector<RemapEntry> entries;
entries.reserve(renderChannelCount);
for (int i = 0; i < mNumSpeakers; ++i)
entries.push_back({i, layout.speakers[i].deviceChannel});
for (int j = 0; j < numSubs; ++j)
entries.push_back({mNumSpeakers + j, layout.subwoofers[j].deviceChannel});
mOutputRouting.buildAuto(std::move(entries), renderChannelCount, deviceChannelCount);
}
mRemap = &mOutputRouting;
renderBlock() — LFE routing (lines 396–404)

No code change required. for (int subCh : mSubwooferChannels) becomes for (int subCh : mSubwooferRenderChannels). The loop body writes to mRenderIO.outBuffer(subCh) with subCh now a render index — correct by construction.

renderBlock() — Phase 6 mix trim (lines 668–686)

Change all isSubwooferChannel(ch) → isSubwooferRenderChannel(ch). These iterate ch = 0..renderChannels-1 (render space).

renderBlock() — Phase 14 render-bus diagnostic (lines 747–828)

Change all isSubwooferChannel(ch) → isSubwooferRenderChannel(ch). These iterate render space. (4 call-sites in this block.)

renderBlock() — Phase 14 device-bus diagnostic (lines 869–944)

Change all isSubwooferChannel(ch) → isSubwooferDeviceChannel(ch). These iterate ch = 0..numOutputChannels-1 (device space). (3 call-sites.)

isSubwooferChannel() — rename + split (line 1049)

Remove isSubwooferChannel().

Add:

// Render-space helper: ch is in 0..renderChannelCount-1
bool isSubwooferRenderChannel(int ch) const {
for (int rc : mSubwooferRenderChannels)
if (rc == ch) return true;
return false;
}
// Device-space helper: ch is in 0..deviceChannelCount-1
bool isSubwooferDeviceChannel(int ch) const {
for (int dc : mSubwooferDeviceChannels)
if (dc == ch) return true;
return false;
}
Private members — changes (lines 1119–1157)

Old New Note
std::vector<int> mSubwooferChannels std::vector<int> mSubwooferRenderChannels render indices N, N+1, …
(new) std::vector<int> mSubwooferDeviceChannels physical device channels from layout
const OutputRemap\* mRemap = nullptr keep, initialized to &mOutputRouting in init() CSV override path remains
(new) OutputRemap mOutputRouting owned routing table; replaces "mAutoRemap" naming
Doc comments — update

Class header lines 12–16: replace old formula with two-space model description
init() comment block: replace "outputChannels" formula with compact render / device split
Lines 62–66 (threading model READ-ONLY list): update mSubwooferChannels → mSubwooferRenderChannels, mSubwooferDeviceChannels
spatial_engine/realtimeEngine/src/OutputRemap.hpp
Add buildAuto() method after the load() method (after line 179):

// Build the routing table from layout-derived entries (no CSV required).
// Called once by Spatializer::init() on the main thread, before start().
// renderChannels: width of internal render bus.
// deviceChannels: width of physical device output bus.
// Out-of-range entries are dropped (should not occur if validation gate passed).
void buildAuto(std::vector<RemapEntry> entries, int renderChannels, int deviceChannels) {
mEntries.clear();
mMaxDeviceIndex = -1;
for (auto& e : entries) {
if (e.layout < 0 || e.layout >= renderChannels) continue;
if (e.device < 0 || e.device >= deviceChannels) continue;
mEntries.push_back(e);
if (e.device > mMaxDeviceIndex) mMaxDeviceIndex = e.device;
}
mIdentity = checkIdentity(renderChannels, deviceChannels);
std::cout << "[OutputRouting] " << mEntries.size()
<< " layout-derived routing entries"
<< (mIdentity ? " — identity, fast-path active" : " — non-identity remap")
<< std::endl;
}
checkIdentity() — update signature to use deviceChannels (line 211):

bool checkIdentity(int renderChannels, int deviceChannels) const {
// Identity requires render and device bus widths to be equal.
// If deviceChannels > renderChannels, there are gap device channels,
// which means routing is non-trivial even if all entries are diagonal.
if (deviceChannels != renderChannels) return false;
if (static_cast<int>(mEntries.size()) != renderChannels) return false;
// ... existing coverage check unchanged ...
}
mIdentity = true default — keep. Default-constructed OutputRemap (no entries) is still identity (used when mOutputRouting is constructed before init() runs).

spatial_engine/realtimeEngine/src/RealtimeTypes.hpp
RealtimeConfig::outputChannels comment (lines 157–166):

Replace the old formula comment with:

// Physical device output bus width — derived from the speaker layout's
// deviceChannel values: deviceChannelCount = max(all .deviceChannel values) + 1.
// Set by Spatializer::init() and read by RealtimeBackend::init() to open AudioIO
// with the correct physical channel count. This is NOT the render bus width.
// Render bus width (numSpeakers + numSubwoofers) is internal to Spatializer.
int outputChannels = 0;
spatial_engine/realtimeEngine/src/EngineSession.cpp
configureRuntime() — remap block (lines 155–165):

mOutputRemap = std::make_unique<OutputRemap>();
if (!mRemapCsv.empty()) {
// LEGACY: CSV remap is deprecated. Layout-derived routing is now standard.
// This path remains temporarily while validation is in progress.
std::cerr << "[EngineSession] WARNING: --remap CSV is deprecated. "
<< "Physical output routing is now derived from the speaker layout JSON. "
<< "CSV support will be removed after validation." << std::endl;
bool remapOk = mOutputRemap->load(mRemapCsv,
mSpatializer->numRenderChannels(), // render bus
mConfig.outputChannels); // device bus
if (remapOk) {
mSpatializer->setRemap(mOutputRemap.get());
} else {
std::cout << "[EngineSession] CSV load failed — retaining layout-derived routing." << std::endl;
}
} else {
std::cout << "[EngineSession] Layout-derived output routing active ("
<< mSpatializer->numRenderChannels() << " render → "
<< mConfig.outputChannels << " device channels)." << std::endl;
// mRemap is already set to &mOutputRouting by Spatializer::init().
}
spatial_engine/realtimeEngine/src/EngineSession.hpp
LayoutInput::remapCsvPath — add deprecation comment:

// DEPRECATED: physical output routing is now derived from layout deviceChannel
// values. This field is retained temporarily as internal scaffolding only.
// Not a supported user workflow. Will be removed after layout-routing validation.
std::string remapCsvPath;
spatial_engine/realtimeEngine/src/main.cpp
Line 159 — --remap flag parsing:

// DEPRECATED: CSV remap path. Not a supported user workflow.
// Retained temporarily as internal scaffolding. Will be removed.
layoutIn.remapCsvPath = getArgString(argc, argv, "--remap");
gui/imgui/src/App.cpp and App.hpp
App.cpp:301–312 — wrap the REMAP CSV UI block in a visible deprecation notice:

ImGui::TextDisabled("REMAP CSV (deprecated — routing now from layout JSON)");
Keep the field and Browse button functional but visually marked.

App.hpp:78 — add comment to mRemapPath:

std::string mRemapPath; // DEPRECATED — remove after layout-routing validation
Naming Rules (enforced throughout patch)
Context Name
Render-space subwoofer indices mSubwooferRenderChannels
Device-space subwoofer channels mSubwooferDeviceChannels
Render-space classification helper isSubwooferRenderChannel(int ch)
Device-space classification helper isSubwooferDeviceChannel(int ch)
Owned routing table object mOutputRouting (type: OutputRemap)
Render bus width renderChannelCount (local), numRenderChannels() (accessor)
Device bus width deviceChannelCount (local), mConfig.outputChannels (config)
Prohibit any reference to "auto-remap" in comments, logs, or names. Use "layout-derived routing" or "output routing table" in all user-visible text.

Invariants After Patch
mRenderIO.channelsOut() == numSpeakers + numSubwoofers — always compact.
mConfig.outputChannels == max(all .deviceChannel) + 1 — device bus only.
mRemap is never null after init() returns true.
mSubwooferRenderChannels[j] == numSpeakers + j always.
isSubwooferRenderChannel(ch) is only called with ch < renderChannelCount.
isSubwooferDeviceChannel(ch) is only called with ch < deviceChannelCount.
No internal DSP code reads mConfig.outputChannels to mean render-bus width.
Identity fast-path is safe: checkIdentity() now requires deviceChannels == renderChannels explicitly, eliminating any ambiguity when device numbering is sparse.
All DBAP math, proximity guard, onset fade, fast-mover sub-stepping are untouched.
init() returns false (logged, engine refuses to start) on any routing validation failure.
Identity Fast-Path Verification
After the patch, verify:

Translab (consecutive deviceChannels 0..17, sub at 18 or consecutive after): mOutputRouting.identity() == true → fast-path copy active → zero overhead vs. pre-patch.
Allosphere (speakers starting at deviceChannel 1, gaps): mOutputRouting.identity() == false → remap path taken.
Mathematical guarantee: identity → deviceChannels == renderChannels necessarily, because all entries have device == layout < renderChannels, so max(device) ≤ renderChannels - 1, so deviceChannelCount ≤ renderChannels. Combined with entries.size() == renderChannels and full bijective coverage, deviceChannelCount == renderChannels. The new checkIdentity() guard makes this explicit rather than relying on the implicit argument.

Verification Steps
Build with ./build.sh --engine-only. Fix any compile errors from renamed members.
Run translab layout: confirm log shows "identity, fast-path active" and audio is correct.
Run Allosphere layout without any --remap flag: confirm log shows routing entries, device channels match layout JSON, audio routes to correct physical outputs.
Deliberately provide a layout with duplicate deviceChannel: confirm hard-fail with descriptive error, engine refuses to start.
Provide a layout with a speaker and subwoofer sharing deviceChannel: same hard-fail.
Run with --remap legacy CSV: confirm deprecation warning in stderr, CSV still applied.
Confirm nanGuardCount stays 0 and Phase 14 render/device masks are stable (no spurious relocation events from the routing change).
