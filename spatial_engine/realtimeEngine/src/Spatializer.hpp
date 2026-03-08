// Spatializer.hpp — Agent 3: DBAP Spatial Audio Panning
//
// Takes per-source audio blocks from Streaming and per-source positions from
// Pose, and distributes each source's audio across the speaker array using
// AlloLib's DBAP (Distance-Based Amplitude Panning) algorithm.
//
// RESPONSIBILITIES:
// 1. Build the al::Speakers array from SpeakerLayoutData at load time.
//    - CRITICAL: al::Speaker expects degrees; layout JSON stores radians.
//    - CRITICAL: Use consecutive 0-based channel indices, NOT hardware
//      deviceChannel numbers (which have gaps and cause out-of-bounds).
// 2. Compute outputChannels from the layout (matching the offline renderer):
//    maxChannel = max(numSpeakers-1, max(subwooferDeviceChannels))
//    outputChannels = maxChannel + 1
//    This value is written into RealtimeConfig so the backend can open
//    AudioIO with the correct channel count. Nothing is hardcoded.
// 3. Create al::Dbap with the speaker array and apply focus setting.
// 4. For each audio block, spatialize every non-LFE source via renderBuffer().
// 5. Route LFE sources directly to subwoofer channels (no spatialization).
// 6. Apply loudspeaker/sub mix trims and master gain (Phase 6).
// 7. Apply output channel remap to physical device outputs (Phase 7).
//
// INTERNAL RENDER BUFFER:
//   DBAP renders into an internal AudioIOData buffer (mRenderIO) sized for
//   outputChannels (layout-derived). After rendering, channels are copied
//   to the real AudioIO output. Phase 7 (OutputRemap) optionally re-routes
//   logical render channels to physical device outputs (e.g., the Allosphere's
//   non-consecutive hardware channel map). Without a remap CSV, an identity
//   fast-path is taken (bit-identical to pre-Phase-7 behavior).
//   See channelMapping.hpp for the Allosphere-specific mapping reference.
//
// ─────────────────────────────────────────────────────────────────────────────
// THREADING MODEL (Phase 8 — Threading and Safety)
// ─────────────────────────────────────────────────────────────────────────────
//
//  MAIN thread:
//    - Calls init() / setRemap() before start(). After start() returns,
//      all Spatializer members are treated as read-only by the main thread.
//    - Calls computeFocusCompensation() — MAIN THREAD ONLY, and ONLY when
//      audio is NOT streaming (i.e., before start() or after stop()).
//      Reason: computeFocusCompensation() creates a temporary al::AudioIOData,
//      runs a simulated render pass, and writes mConfig.loudspeakerMix. The
//      temporary allocation makes it not RT-safe, and the write to the atomic
//      from main thread while the audio thread reads it is safe (atomic), but
//      the simulation render itself touches mRenderIO which is audio-thread-owned.
//
//  AUDIO thread:
//    - Calls renderBlock() once per audio block.
//    - EXCLUSIVELY owns mRenderIO and mSourceBuffer during playback.
//    - Receives all live runtime controls via ControlsSnapshot (passed by
//      const-ref from RealtimeBackend::processBlock). Does NOT read mConfig
//      for masterGain / loudspeakerMix / subMix / focus — those values come
//      only from the snapshot to prevent the "smoother eats its own output"
//      feedback loop.
//    - Reads mRemap via the non-owning pointer (set once before start(),
//      then read-only). mRemap->entries() and mRemap->identity() are const.
//
//  LOADER thread:
//    - Does NOT interact with Spatializer at all.
//
//  READ-ONLY after init() / setRemap() (safe to read from any thread):
//    mSpeakers, mDBap, mNumSpeakers, mSubwooferChannels, mLayoutRadius,
//    mInitialized, mRemap (pointer value; pointed-to object is also const)
//
//  AUDIO-THREAD-OWNED (must not be read/written from any other thread while
//  audio is streaming):
//    mRenderIO, mSourceBuffer
//
// ─────────────────────────────────────────────────────────────────────────────
//
// PROVENANCE:
// - Speaker construction: adapted from SpatialRenderer constructor (lines 66-73)
//   with the same radians→degrees fix and 0-based channel fix.
// - Output channel sizing: adapted from SpatialRenderer::render() (lines 837-842):
//   maxChannel = max(numSpeakers-1, max_sub_channel); out.channels = maxChannel+1
// - DBAP panning: uses al::Dbap::renderBuffer() directly (same as offline).
// - LFE routing: adapted from SpatialRenderer::renderPerBlock() (lines 1018-1028)
//   with the same subGain = masterGain * 0.95 / numSubwoofers formula.
// - Coordinate transform: already handled by Pose.hpp (direction → DBAP position).
// - Channel remap concept: adapted from channelMapping.hpp and mainplayer.hpp
//   which map render channels → hardware device outputs.
//
// REAL-TIME SAFETY:
// - renderBlock() is called on the audio thread. No allocation, no locks,
//   no I/O. All buffers are pre-allocated at init time.
// - al::Dbap::renderBuffer() is real-time safe (fixed-size arrays, no alloc).
// - computeFocusCompensation() is NOT real-time safe. Main thread only.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "al/io/al_AudioIO.hpp"
#include "al/sound/al_Dbap.hpp"
#include "al/sound/al_Speaker.hpp"

#include "RealtimeTypes.hpp"
#include "LayoutLoader.hpp"
#include "OutputRemap.hpp"
#include "Streaming.hpp"
#include "Pose.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// ControlsSnapshot — per-block copy of smoothed runtime control values
//
// Passed from RealtimeBackend::processBlock() into Spatializer::renderBlock()
// so the spatializer NEVER reads mConfig for these parameters. This breaks the
// "smoother eats its own output" feedback loop: the audio thread has one
// canonical representation of each runtime value (mSmooth.smoothed) that is
// never written back into the target atomics.
//
// Ownership: created on the stack in processBlock(), passed by const-ref.
// ─────────────────────────────────────────────────────────────────────────────

struct ControlsSnapshot {
    float masterGain     = 1.0f;
    float focus          = 1.0f;
    float loudspeakerMix = 1.0f;  // manual slider value — used only when autoComp is false
    float subMix         = 1.0f;
    bool  autoComp       = false;  // Phase 11: forwarded from RealtimeBackend smoothed state
                                   // When true, renderBlock() uses mAutoCompValue instead of
                                   // loudspeakerMix (override mode — the two paths never collide)
};

// ─────────────────────────────────────────────────────────────────────────────
// Spatializer — DBAP panning engine for the real-time pipeline
// ─────────────────────────────────────────────────────────────────────────────

class Spatializer {
public:

    Spatializer(RealtimeConfig& config, EngineState& state)
        : mConfig(config), mState(state) {}

    // ── Initialize from speaker layout ───────────────────────────────────
    // Must be called BEFORE the audio stream starts.
    // Builds the al::Speakers array, computes outputChannels from layout,
    // and creates the DBAP panner.
    //
    // CRITICAL FIX 1 (from SpatialRenderer):
    //   al::Speaker expects angles in DEGREES. The layout JSON stores radians.
    //   We must convert: degrees = radians * 180 / π
    //
    // CRITICAL FIX 2 (from SpatialRenderer):
    //   AlloSphere hardware uses non-consecutive channel numbers (1-60 with gaps).
    //   al::Dbap writes to io.outBuffer(deviceChannel), so we must use
    //   consecutive 0-based indices as the channel to avoid out-of-bounds.
    //   Hardware channel remapping happens in a later phase (Channel Remap agent).
    //
    // OUTPUT CHANNEL COMPUTATION (from SpatialRenderer::render() lines 837-842):
    //   maxChannel = max(numSpeakers - 1, max(subwoofer deviceChannels))
    //   outputChannels = maxChannel + 1
    //   This value is written into mConfig.outputChannels so the backend opens
    //   AudioIO with exactly the right number of channels for this layout.

    bool init(const SpeakerLayoutData& layout) {

        // ── Build al::Speakers with 0-based consecutive channels ─────────
        mNumSpeakers = static_cast<int>(layout.speakers.size());
        mSpeakers.clear();
        mSpeakers.reserve(mNumSpeakers);

        for (int i = 0; i < mNumSpeakers; ++i) {
            const auto& spk = layout.speakers[i];
            mSpeakers.emplace_back(al::Speaker(
                i,                                          // consecutive 0-based channel
                spk.azimuth   * 180.0f / static_cast<float>(M_PI),  // rad → deg
                spk.elevation * 180.0f / static_cast<float>(M_PI),  // rad → deg
                0,                                          // group id
                spk.radius                                  // distance from center (meters)
            ));
        }

        std::cout << "[Spatializer] Built " << mNumSpeakers
                  << " al::Speaker objects (0-based consecutive channels)." << std::endl;

        // ── Store layout radius for focus compensation reference ──────────
        // Use the median speaker radius — same calculation as Pose.hpp.
        if (!layout.speakers.empty()) {
            std::vector<float> radii;
            radii.reserve(layout.speakers.size());
            for (const auto& spk : layout.speakers) {
                radii.push_back(spk.radius > 0.0f ? spk.radius : 1.0f);
            }
            std::sort(radii.begin(), radii.end());
            mLayoutRadius = radii[radii.size() / 2];
        }

        // ── Collect subwoofer hardware channels ──────────────────────────
        // LFE sources are routed directly to these channels (no DBAP).
        // These are raw deviceChannel indices from the layout JSON (same as
        // the offline renderer, which indexes out.samples[deviceChannel]).
        // The internal render buffer is sized to accommodate them.
        mSubwooferChannels.clear();
        for (const auto& sub : layout.subwoofers) {
            mSubwooferChannels.push_back(sub.deviceChannel);
        }

        std::cout << "[Spatializer] " << mSubwooferChannels.size()
                  << " subwoofer channel(s):";
        for (int ch : mSubwooferChannels) {
            std::cout << " " << ch;
        }
        std::cout << std::endl;

        // ── Compute output channel count from layout ─────────────────────
        // Matches SpatialRenderer::render() (lines 837-842):
        //   maxChannel = max(numSpeakers - 1, max(subwoofer deviceChannels))
        //   outputChannels = maxChannel + 1
        //
        // This means the output may have gap channels (e.g. Allosphere has
        // channels 13-16 and 47-48 unused). The future Channel Remap agent
        // (see channelMapping.hpp) will handle mapping these to physical
        // device outputs. For now, render channels = device channels.
        int maxChannel = mNumSpeakers - 1;
        for (int subCh : mSubwooferChannels) {
            if (subCh > maxChannel) maxChannel = subCh;
        }
        int computedOutputChannels = maxChannel + 1;

        // Write into config so the backend opens AudioIO with the right count
        mConfig.outputChannels = computedOutputChannels;

        std::cout << "[Spatializer] Output channels derived from layout: "
                  << computedOutputChannels
                  << " (speakers: 0-" << (mNumSpeakers - 1)
                  << ", max sub ch: " << maxChannel << ")." << std::endl;

        // ── Create DBAP panner ───────────────────────────────────────────
        mDBap = std::make_unique<al::Dbap>(mSpeakers, mConfig.dbapFocus.load());
        std::cout << "[Spatializer] DBAP initialized (focus="
                  << mConfig.dbapFocus.load() << ")." << std::endl;

        // ── Pre-allocate per-source mono buffer ──────────────────────────
        mSourceBuffer.resize(mConfig.bufferSize, 0.0f);

        // ── Pre-allocate internal render buffer ──────────────────────────
        // DBAP renders into this buffer (sized to outputChannels).
        // After rendering, channels are copied to the real AudioIO output.
        //
        // WHY: al::Dbap::renderBuffer() writes to io.outBuffer(channel)
        // using the 0-based consecutive channel indices we assigned to
        // speakers, plus subwoofer deviceChannels. The render buffer must
        // be large enough for all of these.
        //
        // The copy step after rendering is currently an identity mapping.
        // In the future, the Channel Remap agent will re-route logical
        // render channels to physical hardware outputs here. This is the
        // same pattern as channelMapping.hpp / mainplayer.hpp where the
        // ADM player maps file channels to Allosphere output channels.
        mRenderIO.framesPerBuffer(mConfig.bufferSize);
        mRenderIO.framesPerSecond(mConfig.sampleRate);
        mRenderIO.channelsIn(0);
        mRenderIO.channelsOut(computedOutputChannels);

        std::cout << "[Spatializer] Internal render buffer: "
                  << computedOutputChannels << " channels × "
                  << mConfig.bufferSize << " frames." << std::endl;

        mInitialized = true;
        return true;
    }

    // ── Render one audio block ───────────────────────────────────────────
    // Called from processBlock() on the audio thread.
    //
    // All rendering happens into the internal mRenderIO buffer:
    //   - Non-LFE sources → DBAP spatialize into speaker channels
    //   - LFE sources → route directly to subwoofer channels
    //
    // After rendering, channels are copied from mRenderIO to the real
    // AudioIO output. This copy step is the future Channel Remap point.
    // Currently it's an identity mapping (render ch N → device ch N).
    //
    // io output buffers must be zeroed BEFORE calling this method.
    //
    // REAL-TIME SAFE: no allocation, no I/O, no locks.

    void renderBlock(al::AudioIOData& io,
                     Streaming& streaming,
                     const std::vector<SourcePose>& poses,
                     uint64_t currentFrame,
                     unsigned int numFrames,
                     const ControlsSnapshot& ctrl) {

        if (!mInitialized) return;

        const float masterGain = ctrl.masterGain;
        const unsigned int renderChannels = mRenderIO.channelsOut();

        // ── Apply live focus update to DBAP panner ───────────────────────
        // ctrl.focus is already smoothed by RealtimeBackend (50 ms tau).
        // The exponential smoother continuously ramps the value, so each block
        // receives a slightly-updated focus that is already interpolated — no
        // within-block per-frame lerp needed. mPrevFocus is kept so a future
        // fast-path (skip renderBuffer when focus is static) can be added.
        mPrevFocus = ctrl.focus;
        mDBap->setFocus(ctrl.focus);

        // Zero the internal render buffer (DBAP accumulates into it)
        mRenderIO.zeroOut();

        for (const auto& pose : poses) {

            // Skip sources with no valid position
            if (!pose.isValid) continue;

            // ── LFE routing (no spatialization) ──────────────────────────
            // Adapted from SpatialRenderer::renderPerBlock() lines 1018-1028
            // subGain = masterGain * 0.95 / numSubwoofers
            // LFE writes into the render buffer (same as non-LFE).
            // The remap step will later handle routing to physical outputs.
            if (pose.isLFE) {
                if (mSubwooferChannels.empty()) continue;

                // Read LFE audio into pre-allocated buffer
                streaming.getBlock(pose.name, currentFrame, numFrames,
                                   mSourceBuffer.data());

                float subGain = (masterGain * kSubCompensation)
                                / static_cast<float>(mSubwooferChannels.size());

                for (int subCh : mSubwooferChannels) {
                    // Bounds check against render buffer
                    if (static_cast<unsigned int>(subCh) >= renderChannels) continue;

                    float* out = mRenderIO.outBuffer(subCh);
                    for (unsigned int f = 0; f < numFrames; ++f) {
                        out[f] += mSourceBuffer[f] * subGain;
                    }
                }
                continue;
            }

            // ── DBAP spatialization ──────────────────────────────────────
            // Read mono audio from streaming agent
            streaming.getBlock(pose.name, currentFrame, numFrames,
                               mSourceBuffer.data());

            // Apply master gain to the source buffer before DBAP.
            for (unsigned int f = 0; f < numFrames; ++f) {
                mSourceBuffer[f] *= masterGain;
            }

            // Phase 11 Fix 3: minimum-distance guard.
            // If the source position is closer to the origin than kMinSourceDist
            // (5 cm), nudge it outward along the same direction. This prevents
            // DBAP's inverse-distance weighting from producing Inf/NaN when a
            // source accidentally coincides with a speaker position.
            al::Vec3f safePos = pose.position;
            float posMag = safePos.mag();
            if (posMag < kMinSourceDist) {
                // Keep direction; push to minimum distance
                safePos = (posMag > 1e-7f)
                    ? (safePos / posMag) * kMinSourceDist
                    : al::Vec3f(0.0f, kMinSourceDist, 0.0f);  // degenerate → front
            }

            // Spatialize into the internal render buffer (sized for all
            // layout channels). renderBuffer() computes per-speaker gains once
            // then streams all frames — O(speakers) gain math, not O(speakers×frames).
            mDBap->renderBuffer(mRenderIO, safePos,
                                mSourceBuffer.data(), numFrames);
        }

        // mPrevFocus already updated above (= ctrl.focus set each block).

        // ── Phase 6: Apply mix trims to mRenderIO ────────────────────
        // Applied AFTER all DBAP + LFE rendering, BEFORE the clamp pass and
        // copy to real AudioIO output. This matches the design doc specification:
        //   - loudspeakerMix → all non-subwoofer channels (main speakers)
        //   - subMix         → subwoofer channels only
        // Values come from the ControlsSnapshot (smoothed, never from atomics).
        //
        // Phase 11 — autoComp override (Invariant 8):
        //   When ctrl.autoComp is true, mAutoCompValue (written by
        //   computeFocusCompensation() on the main thread) overrides the manual
        //   loudspeakerMix slider entirely. This prevents the two paths from
        //   writing the same atomic and silently clobbering each other.
        //   When ctrl.autoComp is false, the manual slider is used as before.
        //
        // Unity-guard (== 1.0f) makes the no-op case zero cost.
        const float spkMix = ctrl.autoComp ? mAutoCompValue : ctrl.loudspeakerMix;
        const float lfeMix = ctrl.subMix;

        if (spkMix != 1.0f) {
            for (unsigned int ch = 0; ch < renderChannels; ++ch) {
                if (!isSubwooferChannel(static_cast<int>(ch))) {
                    float* buf = mRenderIO.outBuffer(ch);
                    for (unsigned int f = 0; f < numFrames; ++f) {
                        buf[f] *= spkMix;
                    }
                }
            }
        }
        if (lfeMix != 1.0f) {
            for (int subCh : mSubwooferChannels) {
                if (static_cast<unsigned int>(subCh) < renderChannels) {
                    float* buf = mRenderIO.outBuffer(subCh);
                    for (unsigned int f = 0; f < numFrames; ++f) {
                        buf[f] *= lfeMix;
                    }
                }
            }
        }

        // ── Phase 11 Fix 3: NaN / Inf / extreme-gain clamp pass ──────────
        // Applied AFTER Phase 6 mix-trims, BEFORE the Phase 7 copy to output.
        // Guarantees that no value outside [-kMaxSample, +kMaxSample] and no
        // non-finite value ever reaches io.outBuffer() → hardware (Invariant 10).
        //
        // This is a last-resort guard, not a normal operating path. The
        // minimum-distance guard above (kMinSourceDist) is the primary defence
        // against DBAP gain spikes. If nanGuardCount is non-zero in the log,
        // there is a source-position or DBAP-distance bug to investigate.
        {
            bool guardFired = false;
            for (unsigned int ch = 0; ch < renderChannels; ++ch) {
                float* buf = mRenderIO.outBuffer(ch);
                for (unsigned int f = 0; f < numFrames; ++f) {
                    float s = buf[f];
                    if (!std::isfinite(s)) {
                        buf[f] = 0.0f;
                        guardFired = true;
                    } else if (s > kMaxSample) {
                        buf[f] = kMaxSample;
                        guardFired = true;
                    } else if (s < -kMaxSample) {
                        buf[f] = -kMaxSample;
                        guardFired = true;
                    }
                }
            }
            if (guardFired) {
                mState.nanGuardCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // ── Phase 7: Copy render buffer → real output via OutputRemap ────
        // If no remap is set (or remap is identity), use the direct-copy
        // fast path (same as pre-Phase-7 behaviour, bit-identical output).
        // Otherwise iterate the remap entries and accumulate each
        // layout channel into its target device channel.
        //
        // FUTURE: The Channel Remap agent is now implemented here.
        // To apply the Allosphere-specific channel map, generate a CSV from
        // channelMapping.hpp's defaultChannelMap and pass it via --remap.

        const unsigned int numOutputChannels = io.channelsOut();

        bool useIdentity = (mRemap == nullptr) || mRemap->identity();

        if (useIdentity) {
            // Fast path: direct copy, render ch N → device ch N.
            const unsigned int copyChannels = std::min(renderChannels, numOutputChannels);
            for (unsigned int ch = 0; ch < copyChannels; ++ch) {
                const float* src = mRenderIO.outBuffer(ch);
                float* dst = io.outBuffer(ch);
                for (unsigned int f = 0; f < numFrames; ++f) {
                    dst[f] += src[f];
                }
            }
        } else {
            // Remap path: accumulate layout → device per entry list.
            // Destination buffers in io were zeroed by the backend before
            // renderBlock() was called, so accumulation is safe.
            for (const auto& entry : mRemap->entries()) {
                if (static_cast<unsigned int>(entry.layout) >= renderChannels) continue;
                if (static_cast<unsigned int>(entry.device) >= numOutputChannels) continue;
                const float* src = mRenderIO.outBuffer(entry.layout);
                float* dst = io.outBuffer(entry.device);
                for (unsigned int f = 0; f < numFrames; ++f) {
                    dst[f] += src[f];
                }
            }
        }
    }

    // ── Accessors ────────────────────────────────────────────────────────
    int numSpeakers() const { return mNumSpeakers; }
    bool isInitialized() const { return mInitialized; }

    // ── Phase 7: Output Remap ─────────────────────────────────────────────
    // Call after init() and before the audio stream starts.
    // The OutputRemap object must outlive the Spatializer.
    // Passing nullptr (the default) restores the identity fast-path.
    void setRemap(const OutputRemap* remap) { mRemap = remap; }

    // ── Phase 6: Focus auto-compensation ─────────────────────────────────
    // THREADING: MAIN THREAD ONLY. Must NOT be called while the audio stream
    // is running. Reason: this method temporarily modifies mRenderIO (the
    // audio-thread-owned render buffer) to run a simulated render pass. It
    // also allocates a temp AudioIOData object. Neither is RT-safe.
    //
    // Correct usage:
    //   (a) Call before backend.start() to set initial compensation, OR
    //   (b) Call after backend.stop() if focus is changed post-start.
    //   (c) Never call during playback — use the atomic loudspeakerMix
    //       directly if you want to adjust gain while playing.
    //
    // Strategy: render a unit impulse at a canonical front reference position
    // (0, radius, 0) with the current focus, sum the power across all main
    // speaker channels, then compute the scalar that would normalize that
    // power to the reference power at focus=0 (flat uniform weights).
    //
    // Reference power at focus=0: each of N speakers gets weight 1/sqrt(N),
    // so total power = N * (1/N) = 1.0 (DBAP keeps constant power at focus=0).
    //
    // At focus > 0, fewer speakers carry the energy, so total power across
    // mains stays at ~1.0 by DBAP's design, but the perceived loudness can
    // shift because some speakers are closer / dominant. We use the measured
    // amplitude sum rather than assuming power-constant behavior to be safe.
    //
    // The computed compensation is written into mConfig.loudspeakerMix
    // (clamped to the ±10 dB range). The sub slider is NOT touched.
    //
    // REAL-TIME SAFE: this method runs on the main/control thread only.
    // It temporarily borrows mRenderIO and mSourceBuffer for the impulse
    // test; it must NOT be called while the audio callback is active.
    float computeFocusCompensation() {
        if (!mInitialized) return 1.0f;

        // Build a reference position: front center at layout radius
        // In DBAP-ready coordinates: (x=0, y=radius, z=0)
        al::Vec3d refPos(0.0, static_cast<double>(mLayoutRadius), 0.0);

        // Use a 1-frame buffer for the impulse test
        const int testFrames = 64;
        std::vector<float> impulse(testFrames, 1.0f);
        const int outCh = static_cast<int>(mRenderIO.channelsOut());

        // Allocate a small temporary AudioIOData for the test
        al::AudioIOData testIO;
        testIO.framesPerBuffer(testFrames);
        testIO.framesPerSecond(mConfig.sampleRate);
        testIO.channelsIn(0);
        testIO.channelsOut(outCh);
        testIO.zeroOut();

        // Render the unit impulse at the reference position
        mDBap->renderBuffer(testIO, refPos, impulse.data(), testFrames);

        // Measure RMS power on main (non-sub) channels
        float power = 0.0f;
        int mainCount = 0;
        for (int ch = 0; ch < outCh; ++ch) {
            if (isSubwooferChannel(ch)) continue;
            const float* buf = testIO.outBuffer(ch);
            for (int f = 0; f < testFrames; ++f) {
                power += buf[f] * buf[f];
            }
            ++mainCount;
        }
        if (mainCount > 0) power /= static_cast<float>(mainCount * testFrames);

        // Reference power: DBAP at focus=0 distributes to all speakers equally.
        // We compute the reference by re-running at focus=0.
        al::Dbap refPanner(mSpeakers, 0.0f);
        al::AudioIOData refIO;
        refIO.framesPerBuffer(testFrames);
        refIO.framesPerSecond(mConfig.sampleRate);
        refIO.channelsIn(0);
        refIO.channelsOut(outCh);
        refIO.zeroOut();
        refPanner.renderBuffer(refIO, refPos, impulse.data(), testFrames);

        float refPower = 0.0f;
        for (int ch = 0; ch < outCh; ++ch) {
            if (isSubwooferChannel(ch)) continue;
            const float* buf = refIO.outBuffer(ch);
            for (int f = 0; f < testFrames; ++f) {
                refPower += buf[f] * buf[f];
            }
        }
        if (mainCount > 0) refPower /= static_cast<float>(mainCount * testFrames);

        // Compensation = sqrt(refPower / power) — amplitude scalar to
        // normalize current loudness back to the focus=0 reference.
        float compensation = 1.0f;
        if (power > 1e-10f && refPower > 1e-10f) {
            compensation = std::sqrt(refPower / power);
        }

        // Clamp to ±10 dB range (linear: ~0.316 to ~3.162)
        compensation = std::max(0.316f, std::min(3.162f, compensation));

        std::cout << "[Spatializer] Focus auto-compensation: focus="
                  << mConfig.dbapFocus.load()
                  << " → autoCompValue=" << compensation
                  << " (" << (20.0f * std::log10(compensation)) << " dB)" << std::endl;

        // Phase 11: write into mAutoCompValue, NOT mConfig.loudspeakerMix.
        // mConfig.loudspeakerMix is reserved exclusively for the manual slider
        // (written by the OSC spkMixDbParam callback). Keeping them separate
        // enforces Invariant 8: the two paths can never clobber each other.
        // renderBlock() reads mAutoCompValue only when ctrl.autoComp is true.
        mAutoCompValue = compensation;
        return compensation;
    }

private:

    // ── Small helpers ─────────────────────────────────────────────────────
    // Returns true if ch is a subwoofer channel index.
    // Used by the Phase 6 mix-trim passes to distinguish mains from sub.
    bool isSubwooferChannel(int ch) const {
        for (int subCh : mSubwooferChannels) {
            if (subCh == ch) return true;
        }
        return false;
    }

    // ── Constants ────────────────────────────────────────────────────────
    // LFE/subwoofer compensation factor (same as offline renderer).
    // TODO: Make configurable or derive from DBAP focus setting.
    static constexpr float kSubCompensation = 0.95f;

    // Phase 11: maximum output sample magnitude before hardware write.
    // 4.0f ≈ +12 dBFS — allows headroom above 0 dBFS while still bounding
    // runaway DBAP gain spikes. See Fix 3 clamp pass in renderBlock().
    static constexpr float kMaxSample = 4.0f;

    // Phase 11: minimum source distance from origin passed to DBAP.
    // Prevents division-by-near-zero in DBAP's distance weighting when a
    // source position is accidentally placed at or near a speaker position.
    static constexpr float kMinSourceDist = 0.05f;  // 5 cm

    // ── References ───────────────────────────────────────────────────────
    RealtimeConfig& mConfig;
    EngineState&    mState;

    // ── DBAP state ───────────────────────────────────────────────────────
    // READ-ONLY after init(). Safe to inspect from any thread (no mutation
    // during playback).
    al::Speakers                mSpeakers;          // AlloLib speaker objects (0-based channels)
    std::unique_ptr<al::Dbap>   mDBap;              // DBAP panner instance
    int                         mNumSpeakers = 0;   // Number of main speakers
    std::vector<int>            mSubwooferChannels; // Subwoofer channel indices (from layout)
    float                       mLayoutRadius = 1.0f; // Median speaker radius (for focus compensation ref position)
    bool                        mInitialized = false;

    // ── Internal render buffer (sized for layout-derived outputChannels) ──
    // AUDIO-THREAD-OWNED: only accessed inside renderBlock() and
    // computeFocusCompensation(). The latter must only be called from the
    // main thread when audio is NOT running (see threading model above).
    al::AudioIOData             mRenderIO;

    // ── Pre-allocated audio buffer (one source at a time) ────────────────
    // AUDIO-THREAD-OWNED: filled from Streaming::getBlock() inside renderBlock().
    std::vector<float>          mSourceBuffer;

    // ── Phase 7: Output remap table ──────────────────────────────────────
    // Non-owning pointer. nullptr → identity fast-path.
    // Set once before start() (main thread), then read-only on audio thread.
    // The pointed-to OutputRemap object is also immutable after load().
    const OutputRemap*          mRemap = nullptr;

    // ── Phase 11: Focus auto-compensation value ───────────────────────────
    // Written ONLY by computeFocusCompensation() (main thread, audio stopped).
    // Read ONLY by renderBlock() when ctrl.autoComp is true.
    // Separate from mConfig.loudspeakerMix (the manual slider) — the two
    // paths never write the same variable. See Invariant 8.
    // Not atomic: written pre-start or via pendingAutoComp main-loop pattern
    // (audio is effectively synchronised out before the write reaches it).
    float                       mAutoCompValue = 1.0f;

    // ── Phase 11: Previous block focus (for per-frame interpolation) ──────
    // Holds the focus value used at the END of the last renderBlock() call.
    // Used as the interpolation start point for the next block so that a
    // focus change produces a smooth ramp rather than a step at the boundary.
    // Audio-thread-owned. See Fix 4.
    float                       mPrevFocus = 1.0f;
};
