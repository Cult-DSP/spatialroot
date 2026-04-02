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

        // ── Pre-cache speaker positions in DBAP-internal space ──────────
        // DBAP::renderBuffer() applies this flip to the source position before
        // computing distances:
        //   relpos = Vec3d(pos.x, -pos.z, pos.y)
        // Speaker positions are stored as mSpeakerVecs[k] = speaker.vec(),
        // which is audio-space Cartesian: (sin(az)*cosEl*r, cos(az)*cosEl*r, sin(el)*r).
        // Distances are then computed as |relpos - mSpeakerVecs[k]|.
        //
        // To make the proximity guard geometrically exact — i.e. to guard
        // against the same distances DBAP actually computes — we must cache
        // speaker positions as speaker.vec() (Vec3d, already available from
        // the al::Speakers array we built) and apply the same source flip
        // before comparing. No separate spherical reconstruction needed.
        mSpeakerPositions.clear();
        mSpeakerPositions.reserve(mNumSpeakers);
        for (int i = 0; i < mNumSpeakers; ++i) {
            // speaker.vec() returns audio-space Cartesian in double precision.
            // Store as float — precision is adequate for a proximity guard.
            al::Vec3d v = mSpeakers[i].vec();
            mSpeakerPositions.emplace_back(
                static_cast<float>(v.x),
                static_cast<float>(v.y),
                static_cast<float>(v.z));
        }
        std::cout << "[Spatializer] Pre-cached " << mSpeakerPositions.size()
                  << " speaker positions (DBAP-internal space) for proximity guard."
                  << std::endl;

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

        // Fix 2 — size the fast-mover scratch buffer (sub-chunk render target).
        {
            int subFrames = std::max(1, mConfig.bufferSize / kNumSubSteps);
            mFastMoverScratch.framesPerBuffer(subFrames);
            mFastMoverScratch.framesPerSecond(mConfig.sampleRate);
            mFastMoverScratch.channelsIn(0);
            mFastMoverScratch.channelsOut(computedOutputChannels);
            std::cout << "[Spatializer] Fast-mover scratch buffer: "
                      << computedOutputChannels << " channels × "
                      << subFrames << " frames (" << kNumSubSteps << " sub-steps)." << std::endl;
        }

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

        for (size_t si = 0; si < poses.size(); ++si) {
            const auto& pose = poses[si];

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

                // Fix 1 — Onset fade (LFE path)
                // Detect whether this block has meaningful signal energy.
                // If the previous block was silent and this one is active,
                // ramp the first kOnsetFadeSamples samples from 0→1 to
                // suppress the step-from-zero low-end pop at source onset.
                if (si < mSourceWasSilent.size()) {
                    float energy = 0.0f;
                    for (unsigned int f = 0; f < numFrames; ++f)
                        energy += mSourceBuffer[f] * mSourceBuffer[f];
                    const bool currentlyActive = (energy > kOnsetEnergyThreshold);
                    if (mSourceWasSilent[si] && currentlyActive) {
                        const unsigned int fadeEnd =
                            std::min(kOnsetFadeSamples, numFrames);
                        for (unsigned int f = 0; f < fadeEnd; ++f)
                            mSourceBuffer[f] *=
                                static_cast<float>(f) / static_cast<float>(fadeEnd);
                    }
                    mSourceWasSilent[si] = currentlyActive ? 0u : 1u;
                }

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
            // Fix 1 — Onset fade (DBAP path)
            // Same gate-and-ramp logic as the LFE path above.
            // Applied before the master-gain multiply so the ramp is not
            // scaled twice and does not affect the proximity guard below.
            if (si < mSourceWasSilent.size()) {
                float energy = 0.0f;
                for (unsigned int f = 0; f < numFrames; ++f)
                    energy += mSourceBuffer[f] * mSourceBuffer[f];
                const bool currentlyActive = (energy > kOnsetEnergyThreshold);
                if (mSourceWasSilent[si] && currentlyActive) {
                    const unsigned int fadeEnd =
                        std::min(kOnsetFadeSamples, numFrames);
                    for (unsigned int f = 0; f < fadeEnd; ++f)
                        mSourceBuffer[f] *=
                            static_cast<float>(f) / static_cast<float>(fadeEnd);
                }
                mSourceWasSilent[si] = currentlyActive ? 0u : 1u;
            }
            // Apply master gain to the source buffer before DBAP.
            for (unsigned int f = 0; f < numFrames; ++f) {
                mSourceBuffer[f] *= masterGain;
            }

            // Phase 13: per-speaker proximity guard (geometrically exact).
            //
            // COORDINATE SPACE:
            //   DBAP::renderBuffer() transforms the source position internally:
            //     relpos = Vec3d(pos.x, -pos.z, pos.y)
            //   Speaker vectors mSpeakerVecs[k] = speaker.vec() are in audio-space
            //   Cartesian (y-forward, x-right, z-up). Distances are computed as:
            //     dist = |relpos - mSpeakerVecs[k]|
            //
            //   Our mSpeakerPositions[] cache stores speaker.vec() exactly.
            //   We apply the same flip to pose.position to get relpos, guard in
            //   that space, then un-flip back to pose space for renderBuffer().
            //
            //   Forward flip  (pose space → DBAP-internal):  (x,y,z) → (x,-z,y)
            //   Inverse flip  (DBAP-internal → pose space):  (x,y,z) → (x,z,-y)
            //   (Both are their own inverse — one application undoes the other.)
            //
            // THRESHOLD: 0.15 m. Worst observed case is source 21.1 at 0.049 m
            // from speaker ch15 at t=47.79 s. 0.15 m gives comfortable clearance
            // without over-constraining trajectory freedom near speakers.

            // Step 1: transform pose.position into DBAP-internal space
            const al::Vec3f& p = pose.position;
            al::Vec3f relpos(p.x, -p.z, p.y);  // same flip DBAP applies internally

            // Step 2: guard in DBAP-internal space (exact geometry)
            //
            // Pass 1 — soft outer zone (single scan, no convergence loop).
            // For sources in (kMinSpeakerDist, kGuardSoftZone), applies a
            // smooth outward bias: zero effect at both zone boundaries,
            // positive outward displacement in between. Prevents the hard-snap
            // DBAP cluster change that occurred when sources crossed kMinSpeakerDist.
            for (const auto& spkVec : mSpeakerPositions) {
                al::Vec3f delta = relpos - spkVec;
                float dist = delta.mag();
                if (dist > kMinSpeakerDist && dist < kGuardSoftZone && dist > 1e-7f) {
                    float u    = (dist - kMinSpeakerDist) / (kGuardSoftZone - kMinSpeakerDist);
                    float push = (kGuardSoftZone - kMinSpeakerDist) * u * (1.0f - u);
                    relpos = spkVec + (delta / dist) * (dist + push);
                }
            }
            //
            // Pass 2 — hard inner floor with convergence loop (unchanged).
            // Catches any source still inside kMinSpeakerDist after Pass 1.
            bool guardFiredForSource = false;
            for (int iter = 0; iter < kGuardMaxIter; ++iter) {
                bool pushed = false;
                for (const auto& spkVec : mSpeakerPositions) {
                    al::Vec3f delta = relpos - spkVec;
                    float dist = delta.mag();
                    if (dist < kMinSpeakerDist) {
                        relpos = spkVec + ((dist > 1e-7f)
                            ? (delta / dist) * kMinSpeakerDist
                            : al::Vec3f(0.0f, kMinSpeakerDist, 0.0f));
                        pushed = true;
                    }
                }
                if (!pushed) break;
                guardFiredForSource = true;
            }
            if (guardFiredForSource) {
                mState.speakerProximityCount.fetch_add(1, std::memory_order_relaxed);
            }

            // Step 3: un-flip back to pose space for renderBuffer()
            // renderBuffer() will re-apply (x,y,z)→(x,-z,y) internally,
            // recovering the guarded relpos we just computed.
            al::Vec3f safePos(relpos.x, relpos.z, -relpos.y);

            // Fix 2 — Fast-mover sub-stepping.
            //
            // Detect whether this source moves more than kFastMoverAngleRad
            // (~14.3°) between block start and block end. If so, split the
            // block into kNumSubSteps equal sub-chunks and render each at an
            // independently guarded, renormalised interpolated position.
            // This converts a block-boundary DBAP gain step into a smooth
            // within-block gain ramp, eliminating the pop at guard-entry and
            // large-motion block boundaries.
            //
            // For static or slow-moving sources the normal single-position
            // path is taken (zero overhead vs. pre-Fix-2 behaviour).
            {
                // Angular span of this block in DBAP position space.
                // positionStart / positionEnd are at mLayoutRadius; normalising
                // projects to unit sphere for the angle comparison.
                al::Vec3f d0 = pose.positionStart.normalized();
                al::Vec3f d1 = pose.positionEnd.normalized();
                float dotVal     = std::clamp(d0.dot(d1), -1.0f, 1.0f);
                float angleDelta = std::acos(dotVal);
                bool  isFastMover = (angleDelta > kFastMoverAngleRad);

                // subFrames is used by both the normal-path doBlend branch
                // and the fast-mover branch — define it here so both can reach it.
                const unsigned int subFrames =
                    numFrames / static_cast<unsigned int>(kNumSubSteps);

                if (!isFastMover) {
                    // ── Normal path ───────────────────────────────────────
                    // Bug 9.1 — cross-block guard-transition continuity.
                    // When Pass 2 fired this block or last block, blend the
                    // guard-resolved position from last block (mPrevSafePos)
                    // into the first half of sub-steps, then safePos for the
                    // second half.  Eliminates the ~23% DBAP gain step at
                    // block boundaries when a source enters or exits the
                    // hard-floor zone.
                    //
                    // doBlend activates only when a guard transition is detected
                    // (current or prior block fired Pass 2) AND a valid prior
                    // position exists.  Both conditions required to avoid
                    // stale-data artefacts on the very first block.
                    const bool doBlend = (si < mPrevSafePos.size())
                        && mPrevSafeValid[si]
                        && (guardFiredForSource || mPrevGuardFired[si]);

                    if (doBlend) {
                        for (int j = 0; j < kNumSubSteps; ++j) {
                            const al::Vec3f& subPos =
                                (j < kNumSubSteps / 2) ? mPrevSafePos[si] : safePos;
                            mFastMoverScratch.zeroOut();
                            mDBap->renderBuffer(mFastMoverScratch, subPos,
                                                mSourceBuffer.data() + j * subFrames,
                                                subFrames);
                            for (unsigned int ch = 0; ch < renderChannels; ++ch) {
                                const float* src = mFastMoverScratch.outBuffer(ch);
                                float*       dst = mRenderIO.outBuffer(ch);
                                for (unsigned int f = 0; f < subFrames; ++f)
                                    dst[j * subFrames + f] += src[f];
                            }
                        }
                    } else {
                        // Normal single-position render (no guard transition).
                        mDBap->renderBuffer(mRenderIO, safePos,
                                            mSourceBuffer.data(), numFrames);
                    }
                } else {
                    // ── Fast-mover path ───────────────────────────────────
                    // Render 4 sub-chunks, each at a lerp'd position that is
                    // renormalised to the layout-radius sphere, then guarded.

                    for (int j = 0; j < kNumSubSteps; ++j) {
                        // Sub-chunk center interpolation weight (0.125, 0.375, 0.625, 0.875)
                        float alpha = (static_cast<float>(j) + 0.5f)
                                      / static_cast<float>(kNumSubSteps);

                        // Lerp in pose space (chord interpolation)
                        al::Vec3f subPose = pose.positionStart
                                            + alpha * (pose.positionEnd - pose.positionStart);

                        // Renormalise back to layout-radius sphere.
                        // The chord midpoint falls slightly inside the sphere;
                        // scaling by mLayoutRadius / mag restores the correct radius.
                        {
                            float mag = subPose.mag();
                            if (mag > 1e-7f) subPose = (subPose / mag) * mLayoutRadius;
                        }

                        // Flip to DBAP-internal space, apply two-pass guard, un-flip
                        al::Vec3f subRelpos(subPose.x, -subPose.z, subPose.y);
                        // Pass 1 — soft outer zone (single scan, mirrors normal path)
                        for (const auto& spkVec : mSpeakerPositions) {
                            al::Vec3f delta = subRelpos - spkVec;
                            float dist = delta.mag();
                            if (dist > kMinSpeakerDist && dist < kGuardSoftZone && dist > 1e-7f) {
                                float u    = (dist - kMinSpeakerDist) / (kGuardSoftZone - kMinSpeakerDist);
                                float push = (kGuardSoftZone - kMinSpeakerDist) * u * (1.0f - u);
                                subRelpos = spkVec + (delta / dist) * (dist + push);
                            }
                        }
                        // Pass 2 — hard inner floor with convergence loop (unchanged)
                        for (int iter = 0; iter < kGuardMaxIter; ++iter) {
                            bool pushed = false;
                            for (const auto& spkVec : mSpeakerPositions) {
                                al::Vec3f delta = subRelpos - spkVec;
                                float dist = delta.mag();
                                if (dist < kMinSpeakerDist) {
                                    subRelpos = spkVec + ((dist > 1e-7f)
                                        ? (delta / dist) * kMinSpeakerDist
                                        : al::Vec3f(0.0f, kMinSpeakerDist, 0.0f));
                                    pushed = true;
                                }
                            }
                            if (!pushed) break;
                        }
                        al::Vec3f subSafePos(subRelpos.x, subRelpos.z, -subRelpos.y);

                        // Render sub-chunk into scratch, then accumulate into
                        // mRenderIO at the correct frame offset.
                        mFastMoverScratch.zeroOut();
                        mDBap->renderBuffer(mFastMoverScratch, subSafePos,
                                            mSourceBuffer.data() + j * subFrames,
                                            subFrames);
                        for (unsigned int ch = 0; ch < renderChannels; ++ch) {
                            const float* src = mFastMoverScratch.outBuffer(ch);
                            float*       dst = mRenderIO.outBuffer(ch);
                            for (unsigned int f = 0; f < subFrames; ++f)
                                dst[j * subFrames + f] += src[f];
                        }
                    }
                }

                // Bug 9.1 — update guard-transition blending state for next block.
                // safePos is the block-center guard-resolved position (normal path).
                // Written unconditionally so a following normal-path block always
                // has a fresh anchor, regardless of whether this block was a
                // fast-mover or not.
                if (si < mPrevSafePos.size()) {
                    mPrevSafePos[si]    = safePos;
                    mPrevSafeValid[si]  = 1u;
                    mPrevGuardFired[si] = guardFiredForSource ? 1u : 0u;
                }
            }
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

        // ── Phase 14 diagnostic: render-bus active-channel mask (pre-copy) ──
        // Measured AFTER all rendering and NaN clamp, BEFORE the OutputRemap
        // copy. Two complementary masks are computed each block:
        //
        //   Active mask  (absolute): channels whose block mean-square exceeds
        //     kRmsThresh = 1e-8 (≈ −80 dBFS). Includes far-field DBAP bleed.
        //     Useful for detecting any channel going fully silent.
        //
        //   Dominant mask (relative): channels whose mean-square is at least
        //     kDomRelThresh (1%, −20 dBFS) of the loudest channel's power.
        //     Tracks the speaker cluster carrying the bulk of spatial energy.
        //     More meaningful for detecting audible channel relocation.
        //
        // Both relocation latches suppress the first-block 0→X false positive
        // (prevMask == 0 guard). Only genuine mid-playback changes fire.
        {
            constexpr float kRmsThresh    = 1e-8f;
            constexpr float kDomRelThresh = 0.01f;  // 1% of max power = −20 dBFS

            uint64_t mask    = 0;
            uint64_t domMask = 0;
            float    maxMs     = 0.0f;
            float    maxMainMs = 0.0f;  // mains-only max, used for domMask reference
            float    mainMs  = 0.0f, subMs = 0.0f;
            float    chMs[64] = {};

            for (unsigned int ch = 0; ch < renderChannels && ch < 64u; ++ch) {
                const float* buf = mRenderIO.outBuffer(ch);
                float ss = 0.0f;
                for (unsigned int f = 0; f < numFrames; ++f) ss += buf[f] * buf[f];
                float ms = ss / static_cast<float>(numFrames);
                chMs[ch] = ms;
                if (ms > kRmsThresh) {
                    mask |= (1ULL << ch);
                    if (isSubwooferChannel(static_cast<int>(ch)))
                        subMs += ms;
                    else
                        mainMs += ms;
                }
                if (ms > maxMs) maxMs = ms;
                if (!isSubwooferChannel(static_cast<int>(ch)) && ms > maxMainMs)
                    maxMainMs = ms;
            }

            // Dominant mask: relative threshold applied after finding per-block max.
            // Guard: only compute if there is meaningful signal (avoids 1.0 * 0 = 0
            // edge case where silence would mark all channels as equally dominant).
            // Reference is maxMainMs (mains only) so sub threshold crossings do not
            // rescale the threshold and cause spurious mains-cluster relocation events.
            // Sub state is captured separately by subRmsTotal.
            const float domThresh = maxMainMs * kDomRelThresh;
            if (domThresh > kRmsThresh) {
                for (unsigned int ch = 0; ch < renderChannels && ch < 64u; ++ch) {
                    if (!isSubwooferChannel(static_cast<int>(ch)) && chMs[ch] >= domThresh)
                        domMask |= (1ULL << ch);
                }
            }

            // Absolute-mask relocation latch — suppress first-block false positive
            uint64_t prevMask = mState.renderActiveMask.load(std::memory_order_relaxed);
            if (mask != prevMask && prevMask != 0) {
                mState.renderRelocPrev.store(prevMask, std::memory_order_relaxed);
                mState.renderRelocNext.store(mask,     std::memory_order_relaxed);
                mState.renderRelocEvent.store(true,    std::memory_order_relaxed);
            }
            mState.renderActiveMask.store(mask, std::memory_order_relaxed);

            // Dominant-mask relocation latch — suppress first-block false positive
            uint64_t prevDom = mState.renderDomMask.load(std::memory_order_relaxed);
            if (domMask != prevDom && prevDom != 0) {
                mState.renderDomRelocPrev.store(prevDom,  std::memory_order_relaxed);
                mState.renderDomRelocNext.store(domMask,  std::memory_order_relaxed);
                mState.renderDomRelocEvent.store(true,    std::memory_order_relaxed);
            }
            mState.renderDomMask.store(domMask, std::memory_order_relaxed);

            // Top-4 mains cluster: find 4 highest-MS main channels via 4-pass linear
            // scan (O(K × channels), K=4). No allocation; RT-safe.
            // A CLUSTER event fires when the new top-4 overlaps the previous by fewer
            // than 3 channels (2+ channels changed) — a meaningful spatial shift.
            {
                uint64_t clusterMask = 0;
                uint64_t picked = 0;
                for (int k = 0; k < 4; ++k) {
                    float best = kRmsThresh;
                    int   bestCh = -1;
                    for (unsigned int ch = 0; ch < renderChannels && ch < 64u; ++ch) {
                        if (isSubwooferChannel(static_cast<int>(ch))) continue;
                        if (picked & (1ULL << ch)) continue;
                        if (chMs[ch] > best) { best = chMs[ch]; bestCh = static_cast<int>(ch); }
                    }
                    if (bestCh >= 0) { clusterMask |= (1ULL << bestCh); picked = clusterMask; }
                }
                uint64_t prevCluster = mState.renderClusterMask.load(std::memory_order_relaxed);
                if (prevCluster != 0) {
                    int overlap = __builtin_popcountll(clusterMask & prevCluster);
                    if (overlap < 3) {
                        mState.renderClusterPrev.store(prevCluster,  std::memory_order_relaxed);
                        mState.renderClusterNext.store(clusterMask,  std::memory_order_relaxed);
                        mState.renderClusterEvent.store(true,        std::memory_order_relaxed);
                    }
                }
                mState.renderClusterMask.store(clusterMask, std::memory_order_relaxed);
            }

            mState.mainRmsTotal.store(std::sqrt(mainMs), std::memory_order_relaxed);
            mState.subRmsTotal.store(std::sqrt(subMs),   std::memory_order_relaxed);
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

        // ── Phase 14 diagnostic: device-output active-channel mask (post-copy) ─
        // Same two-tier approach as the render-bus diagnostic above.
        // Comparing renderDomMask vs deviceDomMask directly shows whether the
        // dominant speaker cluster shifts at the OutputRemap copy step.
        {
            constexpr float kRmsThresh    = 1e-8f;
            constexpr float kDomRelThresh = 0.01f;

            uint64_t mask    = 0;
            uint64_t domMask = 0;
            float    maxMs     = 0.0f;
            float    maxMainMs = 0.0f;  // mains-only max, mirrors render-side logic
            float    chMs[64] = {};

            for (unsigned int ch = 0; ch < numOutputChannels && ch < 64u; ++ch) {
                const float* buf = io.outBuffer(ch);
                float ss = 0.0f;
                for (unsigned int f = 0; f < numFrames; ++f) ss += buf[f] * buf[f];
                float ms = ss / static_cast<float>(numFrames);
                chMs[ch] = ms;
                if (ms > kRmsThresh) mask |= (1ULL << ch);
                if (ms > maxMs) maxMs = ms;
                if (!isSubwooferChannel(static_cast<int>(ch)) && ms > maxMainMs)
                    maxMainMs = ms;
            }

            const float domThresh = maxMainMs * kDomRelThresh;
            if (domThresh > kRmsThresh) {
                for (unsigned int ch = 0; ch < numOutputChannels && ch < 64u; ++ch) {
                    if (!isSubwooferChannel(static_cast<int>(ch)) && chMs[ch] >= domThresh)
                        domMask |= (1ULL << ch);
                }
            }

            uint64_t prevMask = mState.deviceActiveMask.load(std::memory_order_relaxed);
            if (mask != prevMask && prevMask != 0) {
                mState.deviceRelocPrev.store(prevMask, std::memory_order_relaxed);
                mState.deviceRelocNext.store(mask,     std::memory_order_relaxed);
                mState.deviceRelocEvent.store(true,    std::memory_order_relaxed);
            }
            mState.deviceActiveMask.store(mask, std::memory_order_relaxed);

            uint64_t prevDom = mState.deviceDomMask.load(std::memory_order_relaxed);
            if (domMask != prevDom && prevDom != 0) {
                mState.deviceDomRelocPrev.store(prevDom,  std::memory_order_relaxed);
                mState.deviceDomRelocNext.store(domMask,  std::memory_order_relaxed);
                mState.deviceDomRelocEvent.store(true,    std::memory_order_relaxed);
            }
            mState.deviceDomMask.store(domMask, std::memory_order_relaxed);

            // Top-4 mains cluster — mirrors render-side logic above.
            {
                uint64_t clusterMask = 0;
                uint64_t picked = 0;
                for (int k = 0; k < 4; ++k) {
                    float best = kRmsThresh;
                    int   bestCh = -1;
                    for (unsigned int ch = 0; ch < numOutputChannels && ch < 64u; ++ch) {
                        if (isSubwooferChannel(static_cast<int>(ch))) continue;
                        if (picked & (1ULL << ch)) continue;
                        if (chMs[ch] > best) { best = chMs[ch]; bestCh = static_cast<int>(ch); }
                    }
                    if (bestCh >= 0) { clusterMask |= (1ULL << bestCh); picked = clusterMask; }
                }
                uint64_t prevCluster = mState.deviceClusterMask.load(std::memory_order_relaxed);
                if (prevCluster != 0) {
                    int overlap = __builtin_popcountll(clusterMask & prevCluster);
                    if (overlap < 3) {
                        mState.deviceClusterPrev.store(prevCluster,  std::memory_order_relaxed);
                        mState.deviceClusterNext.store(clusterMask,  std::memory_order_relaxed);
                        mState.deviceClusterEvent.store(true,        std::memory_order_relaxed);
                    }
                }
                mState.deviceClusterMask.store(clusterMask, std::memory_order_relaxed);
            }
        }
    }

    // ── Accessors ────────────────────────────────────────────────────────
    int numSpeakers() const { return mNumSpeakers; }
    bool isInitialized() const { return mInitialized; }
    /// Number of channels in the internal render bus (layout-derived).
    /// Safe to call from the main thread after init().
    unsigned int numRenderChannels() const {
        return static_cast<unsigned int>(mRenderIO.channelsOut());
    }

    // ── Phase 7: Output Remap ─────────────────────────────────────────────
    // Call after init() and before the audio stream starts.
    // The OutputRemap object must outlive the Spatializer.
    // Passing nullptr (the default) restores the identity fast-path.
    void setRemap(const OutputRemap* remap) { mRemap = remap; }

    // ── Fix 1: Preallocate per-source onset-fade state ────────────────────
    // MUST be called from the MAIN thread after Pose::loadScene() has
    // established the source count, and BEFORE backend.start().
    // Never call during playback — this method allocates.
    //
    // numSources should equal pose.numSources() (the size of the mPoses
    // vector).  Using a stable, layout-order index (si in renderBlock())
    // avoids any string-keyed lookup inside the audio callback.
    void prepareForSources(size_t numSources) {
        // All sources start "silent" so the very first block always triggers
        // the fade-in ramp regardless of prior state.
        mSourceWasSilent.assign(numSources, 1u);

        // Bug 9.1 — guard-transition blending state. All sources start with
        // no valid prior position and no prior guard firing, so the blend path
        // is suppressed on the very first block (safe default).
        mPrevSafePos.assign(numSources, al::Vec3f(0.0f, 0.0f, 0.0f));
        mPrevSafeValid.assign(numSources, 0u);
        mPrevGuardFired.assign(numSources, 0u);

        std::cout << "[Spatializer] prepareForSources: " << numSources
                  << " per-source state slots allocated (onset-fade + guard-blend)." << std::endl;
    }

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

        // Phase 13: auto-compensation is temporarily disabled.
        // The previous implementation had two structural bugs:
        //   1. The reference position (0, radius, 0) was passed directly to
        //      renderBuffer(), but renderBuffer() applies an internal coord flip
        //      (pos.x, -pos.z, pos.y) before computing distances. The position
        //      that was actually tested was (0, 0, radius) — the top of the sphere,
        //      not the front-center reference intended.
        //   2. At focus=0, DBAP gain = pow(1/(1+dist), 0) = 1.0 for every speaker,
        //      so refPower ≈ N (not 1.0), making the ratio refPower/power
        //      proportional to N² — producing compensation values that hit the
        //      ±10 dB clamp every time.
        // Result: autoComp was applying ~+10 dB unconditionally, making artifacts
        // significantly worse whenever it was enabled.
        //
        // Disabled by returning 1.0f (identity). The mAutoCompValue member,
        // the renderBlock() autoComp branch, and the OSC plumbing are all kept
        // intact so this can be re-enabled once the math is corrected.
        // TODO: reimplement with correct reference position and power model.
        std::cout << "[Spatializer] computeFocusCompensation: disabled (returning 1.0). "
                     "See Phase 13 notes." << std::endl;
        mAutoCompValue = 1.0f;
        return 1.0f;
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
    // runaway DBAP gain spikes. See clamp pass in renderBlock().
    static constexpr float kMaxSample = 4.0f;

    // Phase 12: minimum source-to-speaker distance in DBAP position space.
    // Sources within this radius of any speaker are pushed outward along the
    // source→speaker axis before calling renderBuffer(). This replaces the
    // Phase 11 origin-distance guard (kMinSourceDist) which was geometrically
    // inert (positions always sit at ~mLayoutRadius from the origin).
    // Start value: 0.15 m — conservative first pass.
    // Can be reduced toward 0.05–0.10 m if near-speaker localization sounds
    // artificially constrained after testing.
    static constexpr float kMinSpeakerDist = 0.15f;

    // Soft-repulsion outer boundary.
    // Sources in (kMinSpeakerDist, kGuardSoftZone) receive a smooth outward
    // bias (Pass 1) before the hard-floor convergence loop (Pass 2).
    // The bump amplitude peaks at the zone midpoint (~0.075 m at defaults)
    // and is exactly zero at both boundaries — no discontinuity at entry or
    // at the kMinSpeakerDist handoff to the hard floor.
    static constexpr float kGuardSoftZone = 0.45f;

    // Track A fix — proximity guard convergence.
    // After being pushed away from speaker K, relpos may land inside speaker
    // K+1's zone (adjacent speakers in a dense cluster). Iterating up to
    // kGuardMaxIter times converges to a position outside all speakers' zones
    // simultaneously, eliminating order-dependent sequential-push artifacts.
    // 4 iterations handles all realistic speaker-cluster geometries at 0.15 m
    // threshold without meaningful RT overhead.
    static constexpr int kGuardMaxIter = 4;

    // Fix 2 — Fast-mover sub-stepping constants.
    // kFastMoverAngleRad: minimum angular change between positionStart and
    //   positionEnd (block start → end) that triggers sub-stepping. ~14.3°
    //   matches the offline renderer's Q1/Q3 threshold. Sources with smaller
    //   angular motion use the normal single-position path.
    // kNumSubSteps: number of equal sub-chunks per block when fast-mover is
    //   detected. 4 × 128 = 512 frames at the default buffer size. Each
    //   sub-chunk is rendered at the lerp'd + renormalised position for its
    //   center time, then accumulated into mRenderIO.
    static constexpr float kFastMoverAngleRad = 0.25f;  // ~14.3°
    static constexpr int   kNumSubSteps       = 4;

    // Fix 1 — Onset fade constants.
    // kOnsetEnergyThreshold: sum-of-squares gate for the pre-allocated source
    //   buffer.  getBlock() writes exact 0.0f on silence / past-EOF, so any
    //   real signal will exceed this threshold immediately.
    // kOnsetFadeSamples: linear ramp-in length applied only on the FIRST active
    //   block after silence (~2.7 ms at 48 kHz). Short enough not to smear
    //   transient attacks; long enough to suppress the low-end pop caused by
    //   the abrupt step-from-zero at source onset.
    static constexpr float        kOnsetEnergyThreshold = 1e-10f;
    static constexpr unsigned int kOnsetFadeSamples     = 128u;

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

    // Phase 12: speaker positions in DBAP coordinate space, cached at init().
    // Used by the per-speaker proximity guard in renderBlock().
    // Populated once in init() using the same (x,y,z)→(x,z,−y)×r transform
    // as Pose::directionToDBAPPosition(). READ-ONLY after init().
    std::vector<al::Vec3f>      mSpeakerPositions;

    // ── Internal render buffer (sized for layout-derived outputChannels) ──
    // AUDIO-THREAD-OWNED: only accessed inside renderBlock() and
    // computeFocusCompensation(). The latter must only be called from the
    // main thread when audio is NOT running (see threading model above).
    al::AudioIOData             mRenderIO;

    // Fix 2 — Sub-chunk scratch buffer for fast-mover rendering.
    // Sized to (bufferSize / kNumSubSteps) frames × outputChannels at init().
    // Each fast-mover sub-chunk is rendered into this buffer, then accumulated
    // into mRenderIO at the correct frame offset. Zero-allocation in callback.
    // AUDIO-THREAD-OWNED after init().
    al::AudioIOData             mFastMoverScratch;

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

    // ── Fix 1: Per-source silence-tracking state ──────────────────────────
    // Indexed by stable pose order (si) — same slot each block.
    // 1 = previous block was silent (or not yet initialized).
    // 0 = previous block had signal energy above kOnsetEnergyThreshold.
    // Allocated once by prepareForSources(); zero-size vector = guard
    //   (si < mSourceWasSilent.size() check) keeps audio path safe if
    //   prepareForSources() was never called.
    // AUDIO-THREAD-OWNED after start().
    std::vector<uint8_t>        mSourceWasSilent;

    // ── Bug 9.1: Per-source guard-transition blending state ───────────────
    // Indexed by stable pose order (si) — same slot each block.
    // mPrevSafePos[si]    — guard-resolved position written at end of last
    //                       block (normal path only). Used as the blend-start
    //                       anchor for the next block's doBlend path.
    // mPrevSafeValid[si]  — 1 = mPrevSafePos[si] holds a valid value (at
    //                       least one block has completed for this source).
    // mPrevGuardFired[si] — 1 = hard-floor Pass 2 fired on last block
    //                       (normal path). Triggers blend even when the
    //                       current block is guard-free (exit transition).
    // Allocated once by prepareForSources(). AUDIO-THREAD-OWNED after start().
    std::vector<al::Vec3f>      mPrevSafePos;
    std::vector<uint8_t>        mPrevSafeValid;
    std::vector<uint8_t>        mPrevGuardFired;
};
