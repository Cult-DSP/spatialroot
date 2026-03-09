// RealtimeBackend.hpp — Agent 8: Audio Backend Adapter
//
// Wraps AlloLib's AudioIO to provide a clean interface between the real-time
// engine and the audio hardware. This is the ONLY file that should directly
// touch AudioIO — all other agents interact through the callback chain and
// shared types.
//
// RESPONSIBILITIES:
// 1. Initialize the audio device with the correct sample rate, buffer size,
//    and channel count from RealtimeConfig.
// 2. Register the top-level audio callback.
// 3. Start / stop the audio stream.
// 4. Report CPU load and detect xruns.
//
// PHASE 10 POLISH — Block-level control snapshot + smoothing + pause fade:
// 5. Read all runtime-control atomics ONCE per block into ControlSnapshot.
//    No atomic read occurs inside the per-sample or per-frame inner loops.
// 6. Exponentially smooth toward snapshot targets using tau ≈ 50 ms.
//    Smoothed values are used for all rendering — never the raw snapshots.
// 7. Pause/resume uses a per-sample linear fade (kPauseFadeMs = 8 ms) to
//    avoid hard-mute click transients.
// 8. Per-channel gain anchors (mPrevChannelGains / mNextChannelGains) are
//    reserved for future block-boundary gain interpolation to prevent
//    speaker-switch clicks. Currently identity (placeholder).
//
// DESIGN NOTES:
// - The callback function is static (required by AlloLib's C-style callback).
//   It receives `this` via the userData pointer and dispatches to the member
//   function `processBlock()`.
// - All state added for Phase 10 polish is POD (no heap in callback hot-path).
// - SmoothedState::tauSec is tunable at construction time (default 50 ms).
// - The callback must NEVER allocate, lock, or do I/O.
//
// REFERENCE: AlloLib AudioIO API (thirdparty/allolib/include/al/io/al_AudioIO.hpp)
//   AudioIO::init(callback, userData, framesPerBuf, framesPerSec, outChans, inChans)
//   AudioIO::open() / start() / stop() / close()
//   AudioIO::cpu() → current audio thread CPU load
//   AudioIOData::out(chan, frame) → write to output buffer
//   AudioIOData::framesPerBuffer() → number of frames in current callback
//   AudioIOData::channelsOut() → number of output channels

#pragma once

#include <cmath>    // std::exp (used for per-block smoothing in processBlock)
#include <iostream>
#include <string>
#include <functional>
#include <cstring>  // memset, memcpy
#include <vector>
#include <algorithm> // std::min

#include "al/io/al_AudioIO.hpp"

#include "RealtimeTypes.hpp"
#include "Streaming.hpp"      // Streaming — needed for inline processBlock()
#include "Pose.hpp"           // Pose — needed for inline processBlock()
#include "Spatializer.hpp"    // Spatializer — needed for inline processBlock()

// ─────────────────────────────────────────────────────────────────────────────
// RealtimeBackend — AlloLib AudioIO wrapper for the real-time engine
// ─────────────────────────────────────────────────────────────────────────────

class RealtimeBackend {
public:

    // ── Constructor / Destructor ─────────────────────────────────────────

    RealtimeBackend(RealtimeConfig& config, EngineState& state)
        : mConfig(config), mState(state) {}

    ~RealtimeBackend() {
        shutdown();
    }

    // ── Lifecycle ────────────────────────────────────────────────────────

    /// Initialize the audio device. Must be called before start().
    /// Returns true on success.
    bool init() {
        std::cout << "[Backend] Initializing audio device..." << std::endl;
        std::cout << "  Sample rate:      " << mConfig.sampleRate << " Hz" << std::endl;
        std::cout << "  Buffer size:      " << mConfig.bufferSize << " frames" << std::endl;
        std::cout << "  Output channels:  " << mConfig.outputChannels << std::endl;
        std::cout << "  Input channels:   " << mConfig.inputChannels << std::endl;

        // Register the static callback with 'this' as userData so we can
        // dispatch into the member function processBlock().
        mAudioIO.init(
            audioCallback,              // static callback function
            this,                       // userData → passed back in callback
            mConfig.bufferSize,         // frames per buffer
            (double)mConfig.sampleRate, // sample rate
            mConfig.outputChannels,     // output channels
            mConfig.inputChannels       // input channels
        );

        // Open the device (allocates hardware buffers)
        if (!mAudioIO.open()) {
            std::cerr << "[Backend] ERROR: Failed to open audio device." << std::endl;
            return false;
        }

        std::cout << "[Backend] Audio device opened successfully." << std::endl;

        // Report actual device parameters (may differ from requested).
        // AlloLib/PortAudio negotiates with the OS default device; the actual
        // channel count may be lower than requested if the wrong device is
        // selected (e.g., MacBook built-in instead of MOTU).
        const int actualOutChannels = static_cast<int>(mAudioIO.channelsOut());
        std::cout << "  Actual output channels: " << actualOutChannels << std::endl;
        std::cout << "  Actual buffer size:     " << mAudioIO.framesPerBuffer() << std::endl;

        // ── Post-open channel count validation ────────────────────────────
        // The layout requires exactly mConfig.outputChannels output channels.
        // If the device opened with fewer, any render channel ≥ actualOutChannels
        // would be silently dropped by the Spatializer copy step, producing
        // missing speakers / wrong-channel output. This is never acceptable.
        // Refuse to start rather than run with an incorrect channel mapping.
        if (actualOutChannels < mConfig.outputChannels) {
            std::cerr << "[Backend] FATAL: Audio device opened with only "
                      << actualOutChannels << " output channel(s), but the "
                      << "speaker layout requires " << mConfig.outputChannels
                      << " channel(s).\n"
                      << "  → Check macOS System Settings > Sound: is the correct "
                      << "output device (e.g., MOTU) set as default?\n"
                      << "  → Aborting — refusing to start with incorrect channel "
                      << "count (silent speaker drops are not acceptable)."
                      << std::endl;
            mAudioIO.close();
            return false;
        }

        if (actualOutChannels > mConfig.outputChannels) {
            std::cout << "  [Backend] INFO: Device provides " << actualOutChannels
                      << " channels; layout uses " << mConfig.outputChannels
                      << ". Extra hardware channels will be unused." << std::endl;
        }

        mInitialized = true;
        return true;
    }

    /// Start audio streaming. Returns true on success.
    bool start() {
        if (!mInitialized) {
            std::cerr << "[Backend] ERROR: Cannot start — not initialized." << std::endl;
            return false;
        }
        std::cout << "[Backend] Starting audio stream..." << std::endl;

        if (!mAudioIO.start()) {
            std::cerr << "[Backend] ERROR: Failed to start audio stream." << std::endl;
            return false;
        }

        mConfig.playing.store(true);
        std::cout << "[Backend] Audio stream started." << std::endl;
        return true;
    }

    /// Stop audio streaming.
    void stop() {
        if (mAudioIO.isRunning()) {
            std::cout << "[Backend] Stopping audio stream..." << std::endl;
            mAudioIO.stop();
            mConfig.playing.store(false);
            std::cout << "[Backend] Audio stream stopped." << std::endl;
        }
    }

    /// Full shutdown: stop stream and close device.
    void shutdown() {
        stop();
        if (mInitialized) {
            mAudioIO.close();
            mInitialized = false;
            std::cout << "[Backend] Audio device closed." << std::endl;
        }
    }

    // ── Status queries ───────────────────────────────────────────────────

    /// Current CPU load of the audio thread (0.0–1.0).
    double cpuLoad() const { return mAudioIO.cpu(); }

    /// Whether the audio stream is currently running.
    bool isRunning() { return mAudioIO.isRunning(); }

    /// Whether the device has been initialized.
    bool isInitialized() const { return mInitialized; }

    // ── Access to underlying AudioIO (for future agent chaining) ─────────

    /// Returns a reference to the AudioIO object.
    /// Used by agents that need to append their own AudioCallbacks.
    al::AudioIO& audioIO() { return mAudioIO; }

    // ── Agent wiring ─────────────────────────────────────────────────────
    //
    // The backend holds raw pointers to agents. Ownership stays with main().
    // Pointers are set once before start() and never change during audio
    // streaming — no synchronization needed.

    /// Connect the streaming agent. Must be called BEFORE start().
    void setStreaming(Streaming* agent) {
        mStreamer = agent;
    }

    /// Connect the pose agent. Must be called BEFORE start().
    void setPose(Pose* agent) {
        mPose = agent;
    }

    /// Connect the spatializer agent. Must be called BEFORE start().
    void setSpatializer(Spatializer* agent) {
        mSpatializer = agent;
    }

    /// Cache source names from the streaming agent for use in processBlock().
    /// Must be called AFTER loadScene() and BEFORE start().
    void cacheSourceNames(const std::vector<std::string>& names) {
        mSourceNames = names;
        // Pre-allocate the mono mix buffer for the largest possible block.
        // This avoids allocation on the audio thread.
        mMonoMixBuffer.resize(mConfig.bufferSize, 0.0f);
        std::cout << "[Backend] Cached " << mSourceNames.size()
                  << " source names for audio callback." << std::endl;
    }


private:

    // ── Static audio callback (C-style, required by AlloLib) ─────────────
    //
    // AlloLib calls this on the audio thread. We recover 'this' from
    // userData and dispatch to the member function.

    static void audioCallback(al::AudioIOData& io) {
        RealtimeBackend* self = static_cast<RealtimeBackend*>(io.user());
        if (self) {
            self->processBlock(io);
        }
    }

    // ── Per-block processing (called on audio thread) ────────────────────
    //
    // Full spatial rendering pipeline (all phases integrated):
    //   1. Zero output buffers
    //   2. Pose agent computes per-source positions (SLERP + layout transform)
    //   3. Spatializer distributes each source via DBAP across speakers;
    //      LFE sources are routed directly to subwoofer channels (Phase 2/4);
    //      loudspeaker/sub mix trims applied after DBAP (Phase 6);
    //      output channel remap applied before copy-to-device (Phase 7).
    //   4. Update EngineState frame counter + playback time
    //   5. CPU load monitoring
    //
    // THREADING:  All code here runs on the AUDIO THREAD exclusively.
    //             See RealtimeTypes.hpp for the full threading model.
    //
    // REAL-TIME CONTRACT:
    // - No allocation, no locks, no I/O.
    // - Streaming read is lock-free (double-buffered, atomic state flags).
    // - Pose read is single-threaded (audio thread owns mPoses/mLastGoodDir).
    // - All EngineState writes use memory_order_relaxed (single writer here;
    //   main/loader threads only poll for display, one-buffer lag is fine).

    void processBlock(al::AudioIOData& io) {

        const unsigned int numFrames  = static_cast<unsigned int>(io.framesPerBuffer());
        const unsigned int numChannels= static_cast<unsigned int>(io.channelsOut());
        // Use mConfig.sampleRate (int) cast to double for per-block time math.
        const double sampleRate       = static_cast<double>(mConfig.sampleRate);
        const double blockDurSec      = static_cast<double>(numFrames) / sampleRate;

        // ── A) Snapshot all runtime-control atomics ONCE at block start ──────
        // Nothing below this point in the block should read the config atomics.
        // This keeps the audio thread free of repeated atomic traffic inside the
        // Spatializer / mixing inner loops.
        {
            ControlSnapshot& t  = mSmooth.target;
            t.masterGain        = mConfig.masterGain.load(std::memory_order_relaxed);
            t.focus             = mConfig.dbapFocus.load(std::memory_order_relaxed);
            t.loudspeakerMix    = mConfig.loudspeakerMix.load(std::memory_order_relaxed);
            t.subMix            = mConfig.subMix.load(std::memory_order_relaxed);
            t.autoComp          = mConfig.focusAutoCompensation.load(std::memory_order_relaxed);
        }

        // ── B) Exponential smoothing toward snapshot targets (per-block) ─────
        // alpha = 1 − exp(−dt / τ).  One std::exp call per block — negligible.
        // Uses the smoothed values for rendering so that rapid OSC slider moves
        // produce a gradual ramp rather than a step discontinuity.
        {
            const double alpha = (mSmooth.tauSec > 0.0)
                ? 1.0 - std::exp(-blockDurSec / mSmooth.tauSec)
                : 1.0;
            ControlSnapshot&       s   = mSmooth.smoothed;
            const ControlSnapshot& tgt = mSmooth.target;
            s.masterGain     = s.masterGain     + static_cast<float>(alpha * (tgt.masterGain     - s.masterGain));
            s.focus          = s.focus          + static_cast<float>(alpha * (tgt.focus          - s.focus));
            s.loudspeakerMix = s.loudspeakerMix + static_cast<float>(alpha * (tgt.loudspeakerMix - s.loudspeakerMix));
            s.subMix         = s.subMix         + static_cast<float>(alpha * (tgt.subMix         - s.subMix));
            s.autoComp       = tgt.autoComp;  // bool: take target immediately
        }

        // ── C) Pause-fade: detect edge on paused flag, arm fade ramp ─────────
        // Read paused ONCE here (already separate from ControlSnapshot).
        // On a pause edge  (playing → paused): arm a fade-OUT (gain 1→0).
        // On a resume edge (paused → playing): arm a fade-IN  (gain 0→1).
        // The per-sample ramp in Step D prevents hard-mute click transients.
        const bool pausedNow = mConfig.paused.load(std::memory_order_relaxed);
        if (pausedNow != mPrevPaused) {
            const unsigned int fadeFrames = std::max(1u,
                static_cast<unsigned int>((kPauseFadeMs / 1000.0) * sampleRate));
            if (pausedNow) {
                // playing → paused: fade OUT  (mPauseFade → 0.0)
                mPauseFadeFramesLeft = fadeFrames;
                mPauseFadeStep       = -(mPauseFade / static_cast<float>(fadeFrames));
            } else {
                // paused → playing: fade IN   (mPauseFade → 1.0)
                mPauseFade           = 0.0f;
                mPauseFadeFramesLeft = fadeFrames;
                mPauseFadeStep       = 1.0f / static_cast<float>(fadeFrames);
            }
            mPrevPaused = pausedNow;
        }

        // ── D) Per-channel gain anchors (block-boundary interpolation) ────────
        // Resize once if channel count changes (init or device change).
        // mPrevChannelGains holds the gains applied at the end of the last block;
        // mNextChannelGains will hold the target gains for this block.
        // Per-frame lerp across the block eliminates speaker-switch clicks.
        // Currently identity — future work: populate from Spatializer top-K.
        if (mNextChannelGains.size() != numChannels) {
            mPrevChannelGains.assign(numChannels, 1.0f);
            mNextChannelGains.assign(numChannels, 1.0f);
        }
        for (unsigned int c = 0; c < numChannels; ++c) {
            mPrevChannelGains[c] = mNextChannelGains[c]; // shift: last→prev
            mNextChannelGains[c] = 1.0f;                 // TODO: per-source DBAP top-K
        }

        // ── Step 1: Zero all output channels ─────────────────────────────────
        for (unsigned int ch = 0; ch < numChannels; ++ch)
            std::memset(io.outBuffer(ch), 0, numFrames * sizeof(float));

        // ── Step 2: Compute source positions for this block ───────────────────
        if (mPose) {
            const uint64_t curFrame    = mState.frameCounter.load(std::memory_order_relaxed);
            const double   blockCtrSec = static_cast<double>(curFrame + numFrames / 2) / sampleRate;
            mPose->computePositions(blockCtrSec);
        }

        // ── Step 3: Spatialize all sources via DBAP ───────────────────────────
        // Build a ControlsSnapshot from the smoothed values and pass it directly
        // into renderBlock(). The spatializer reads ONLY from this snapshot —
        // it never touches mConfig for these parameters.
        //
        // CRITICAL: we do NOT write smoothed values back into mConfig here.
        // Writing back would corrupt the target atomics (turning the smoother's
        // output into the next block's target), causing the "smoother eats its
        // own output" feedback loop where parameters appear stuck or barely move.
        //
        // mConfig atomics are WRITE-ONLY from the audio thread's perspective:
        //   - Written by: OSC listener thread (the true source of truth)
        //   - Read  by:   audio thread Step A snapshot only
        //   - Never written by: audio thread (no write-back ever)
        if (mSpatializer && mStreamer && mPose) {
            ControlsSnapshot ctrl;
            ctrl.masterGain     = mSmooth.smoothed.masterGain;
            ctrl.focus          = mSmooth.smoothed.focus;
            ctrl.loudspeakerMix = mSmooth.smoothed.loudspeakerMix;
            ctrl.subMix         = mSmooth.smoothed.subMix;
            ctrl.autoComp       = mSmooth.smoothed.autoComp;  // Phase 11: Fix 1

            const uint64_t currentFrame = mState.frameCounter.load(std::memory_order_relaxed);
            mSpatializer->renderBlock(io, *mStreamer, mPose->getPoses(),
                                      currentFrame, numFrames, ctrl);
        }

        // ── Step 4: Apply pause fade per-sample ──────────────────────────────
        // After all rendering, scale output samples by mPauseFade.
        // If fully paused (mPauseFade == 0 and no fade in progress), clear
        // buffers and return — skips state updates to keep position stable.
        if (mPauseFadeFramesLeft > 0 || mPauseFade < 1.0f) {
            for (unsigned int f = 0; f < numFrames; ++f) {
                // Advance fade ramp one sample at a time.
                if (mPauseFadeFramesLeft > 0) {
                    mPauseFade += mPauseFadeStep;
                    mPauseFade  = std::max(0.0f, std::min(1.0f, mPauseFade));
                    --mPauseFadeFramesLeft;
                }
                // Apply current fade gain to every output channel.
                const float fadeGain = mPauseFade;
                for (unsigned int ch = 0; ch < numChannels; ++ch)
                    io.outBuffer(ch)[f] *= fadeGain;
            }
        }

        // If fully paused (fade complete, gain == 0) — zero outputs and return
        // without advancing playback position counters.
        if (pausedNow && mPauseFadeFramesLeft == 0 && mPauseFade <= 0.0f) {
            for (unsigned int ch = 0; ch < numChannels; ++ch)
                std::memset(io.outBuffer(ch), 0, numFrames * sizeof(float));
            // Update only CPU load — do NOT advance frameCounter.
            mState.cpuLoad.store(
                std::max(0.0f, std::min(1.0f, static_cast<float>(mAudioIO.cpu()))),
                std::memory_order_relaxed);
            return;
        }

        // ── Step 5: Update engine state ───────────────────────────────────────
        const uint64_t prevFrames = mState.frameCounter.load(std::memory_order_relaxed);
        const uint64_t newFrames  = prevFrames + numFrames;
        mState.frameCounter.store(newFrames, std::memory_order_relaxed);
        mState.playbackTimeSec.store(
            static_cast<double>(newFrames) / sampleRate, std::memory_order_relaxed);

        // ── Step 6: CPU load monitoring ───────────────────────────────────────
        mState.cpuLoad.store(
            std::max(0.0f, std::min(1.0f, static_cast<float>(mAudioIO.cpu()))),
            std::memory_order_relaxed);
    }

    // ── Member data ──────────────────────────────────────────────────────

    RealtimeConfig& mConfig;    // Reference to shared config (set at startup)
    EngineState&    mState;     // Reference to shared engine state
    al::AudioIO     mAudioIO;   // AlloLib audio device wrapper
    bool            mInitialized = false;

    // ── Agent pointers (set once before start(), never changed) ──────────
    // THREADING: Set on the MAIN thread before start(). After start() these
    // are read-only on the AUDIO thread. No synchronization needed —
    // start() provides the required happens-before relationship.
    Streaming*    mStreamer     = nullptr;
    Pose*         mPose         = nullptr;
    Spatializer*  mSpatializer  = nullptr;

    // ── Cached data for audio callback (set once, read-only in callback) ─
    // THREADING: Written by main thread (cacheSourceNames) before start(),
    // then only read on the audio thread. Same happens-before as agent ptrs.
    std::vector<std::string> mSourceNames;   // Source name list (reserved for future use)
    std::vector<float>       mMonoMixBuffer; // Pre-allocated temp buffer (reserved for future use)

    // ── Phase 10 polish: per-block control snapshot + exponential smoothing ──
    //
    // ControlSnapshot holds one atomic read per parameter, taken ONCE at the
    // very top of processBlock(). Nothing inside the block reads config atomics
    // again. Avoids repeated atomic traffic inside Spatializer inner loops.
    //
    // SmoothedState::smoothed is the audio thread's ONLY representation of
    // runtime control values. It is passed to renderBlock() via ControlsSnapshot.
    // It is NEVER written back into mConfig atomics — those remain the exclusive
    // domain of the OSC/GUI writer threads.
    //
    // NOTE: elevationMode is intentionally NOT in this snapshot. It is a discrete
    // enum read directly by Pose::computePositions() via its own relaxed atomic
    // load. Smoothing a discrete enum index makes no sense, and Pose is the only
    // consumer, so routing it through the backend snapshot would add complexity
    // for no benefit.
    //
    // tau = 50 ms → α ≈ 0.52 at 512/48k (10.7 ms block) → ~4 blocks to 95%.
    // Audibly this means a slider move smooths over ~200 ms — imperceptible
    // for gain/focus changes but eliminates step discontinuities.
    //
    // THREADING: All fields below are written AND read exclusively on the
    // AUDIO thread. No synchronization required.

    struct ControlSnapshot {
        float masterGain     = 1.0f;
        float focus          = 1.0f;
        float loudspeakerMix = 1.0f;
        float subMix         = 1.0f;
        bool  autoComp       = false;
    };

    struct SmoothedState {
        ControlSnapshot smoothed;        // current smoothed values (used for rendering)
        ControlSnapshot target;          // latest snapshot from atomics (updated each block)
        double          tauSec = 0.050;  // smoothing time constant (default 50 ms)
    } mSmooth;

    // ── Phase 10 polish: pause fade ───────────────────────────────────────────
    //
    // Hard-muting on pause causes an audible click transient. Instead we ramp
    // the output gain linearly over kPauseFadeMs before going silent (fade-out)
    // and after resuming (fade-in). The ramp is applied per-sample after all
    // rendering in processBlock Step 4.
    //
    // State machine:
    //   playing: mPauseFade == 1.0, mPauseFadeFramesLeft == 0
    //   fading out: mPauseFade 1→0 over kPauseFadeMs frames
    //   fully paused: mPauseFade == 0, buffers cleared, frameCounter NOT advanced
    //   fading in: mPauseFade 0→1 over kPauseFadeMs frames
    //
    // THREADING: Audio thread only.

    static constexpr double kPauseFadeMs = 8.0; // 8 ms is enough to mask click

    bool         mPrevPaused          = false;  // paused state seen last block
    float        mPauseFade           = 1.0f;   // current fade envelope (0=silent, 1=full)
    float        mPauseFadeStep       = 0.0f;   // per-sample delta (negative=fade-out, positive=fade-in)
    unsigned int mPauseFadeFramesLeft = 0;       // samples remaining in current ramp

    // ── Phase 10 polish: per-channel gain anchors (block-boundary ramp) ──────
    //
    // Keeping prev/next per-output-channel gain allows us to linearly interpolate
    // across a block and prevent step discontinuities when DBAP speaker sets
    // switch (e.g., due to a focus change or abrupt position jump).
    //
    // Currently identity (all gains == 1.0) — placeholder for future top-K
    // per-source gain precomputation. When implemented, mNextChannelGains will
    // be filled with the block-end spatializer gains before rendering, and the
    // copy-to-output loop will lerp between prev and next per-frame.
    //
    // THREADING: Audio thread only.

    std::vector<float> mPrevChannelGains; // gains at start of current block (end of last block)
    std::vector<float> mNextChannelGains; // gains at end of current block (computed each block)
};

