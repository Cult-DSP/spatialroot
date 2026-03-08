// RealtimeTypes.hpp — Shared data types for the real-time spatial audio engine
//
// These structs are used across multiple agents (Backend, Streaming, Pose,
// Spatializer, etc.) to pass data through the processing pipeline.
//
// ─────────────────────────────────────────────────────────────────────────────
// THREADING MODEL (Phase 8 — Threading and Safety)
// ─────────────────────────────────────────────────────────────────────────────
//
// The engine uses THREE threads:
//
//  ┌───────────────┬───────────────────────────────────────────────────────┐
//  │ Thread        │ Role                                                  │
//  ├───────────────┼───────────────────────────────────────────────────────┤
//  │ MAIN thread   │ Setup, monitoring loop, clean shutdown. Owns all      │
//  │               │ agent object lifetimes. Calls computeFocusComp(),     │
//  │               │ reads EngineState atomics for display.                │
//  ├───────────────┼───────────────────────────────────────────────────────┤
//  │ AUDIO thread  │ AlloLib AudioIO callback at real-time priority.       │
//  │               │ Runs audioCallback() → processBlock() every buffer.   │
//  │               │ MUST NOT allocate, lock, or do I/O.                  │
//  │               │ Owns: EngineState writes, mPoses (via Pose),          │
//  │               │       mLastGoodDir (via Pose), mRenderIO (Spatializer)│
//  ├───────────────┼───────────────────────────────────────────────────────┤
//  │ LOADER thread │ Background WAV streaming (Streaming::loaderWorker()). │
//  │               │ Reads next audio chunk from disk into the inactive    │
//  │               │ double-buffer slot. Owns: SNDFILE* via fileMutex,     │
//  │               │ inactive buffer write (with memory_order_release).    │
//  └───────────────┴───────────────────────────────────────────────────────┘
//
// MEMORY ORDERING RULES:
//
//  ┌─────────────────────────────┬────────────────────────────────────────┐
//  │ Atomic                      │ Ordering used                          │
//  ├─────────────────────────────┼────────────────────────────────────────┤
//  │ RealtimeConfig::masterGain  │ relaxed (audio reads; GUI writes — OK  │
//  │ ::loudspeakerMix, ::subMix  │ because stale value = smooth fade,     │
//  │                             │ not a data race on non-atomic data)    │
//  │ ::dbapFocus                 │ relaxed (audio snapshots in step A;    │
//  │                             │ OSC listener writes — one-block lag    │
//  │                             │ is inaudible. Was plain float before,  │
//  │                             │ which was a data race — now fixed.)    │
//  │ ::focusAutoCompensation     │ relaxed (same reasoning)               │
//  │ ::elevationMode             │ relaxed (stale-by-one-block is fine;   │
//  │                             │ mode switch is not sample-accurate)    │
//  │ ::playing, ::shouldExit     │ relaxed (polling-only, no dep. data)   │
//  │ ::paused                    │ relaxed (ParameterServer listener       │
//  │                             │ writes; audio thread polls — one-buffer │
//  │                             │ lag inaudible, not a data race)         │
//  ├─────────────────────────────┼────────────────────────────────────────┤
//  │ EngineState::frameCounter   │ relaxed (single writer: audio thread;  │
//  │ ::playbackTimeSec           │ readers: main/loader for monitoring —  │
//  │ ::cpuLoad, ::xrunCount      │ display lag of one buffer is fine)     │
//  ├─────────────────────────────┼────────────────────────────────────────┤
//  │ SourceStream::stateA/B      │ release on write (loader & audio)      │
//  │ ::chunkStartA/B             │ acquire on read (audio & loader)       │
//  │ ::validFramesA/B            │ Forms a acquire/release pair that      │
//  │ ::activeBuffer              │ ensures buffer data is visible before  │
//  │                             │ the READY/PLAYING state flip.          │
//  ├─────────────────────────────┼────────────────────────────────────────┤
//  │ Streaming::mLoaderRunning   │ release on write (main sets false);    │
//  │                             │ acquire on read (loader polls in loop) │
//  └─────────────────────────────┴────────────────────────────────────────┘
//
// INVARIANTS THAT MUST NEVER BE VIOLATED:
//
//  1. Agent pointers in RealtimeBackend (mStreamer, mPose, mSpatializer) are
//     set ONCE before Backend::start() and NEVER changed while audio runs.
//     No atomic or lock is needed — the happens-before from start() covers it.
//
//  2. All agent data structures (mStreams, mPoses, mSourceOrder, mSources)
//     are fully populated before start() and never modified during playback.
//     The audio thread only reads them.
//
//  3. Streaming::shutdown() MUST be called only AFTER Backend::stop() returns.
//     The audio thread calls getSample()/getBlock() via mStreamer. After stop()
//     returns, no more audio callbacks will fire. Only then is it safe to
//     clear mStreams (which destroys the SourceStream objects).
//
//  4. Pose::computePositions() is owned EXCLUSIVELY by the audio thread.
//     mLastGoodDir and mPoses are written only there. getPoses() returns a
//     const reference safe to read from within the same processBlock() call.
//
//  5. Spatializer::computeFocusCompensation() MUST be called from the MAIN
//     thread only, and ONLY when audio is not streaming (i.e., before start()
//     or after stop()). It performs temporary allocation and DBAP panning
//     that are not RT-safe.
//
//  6. The loader thread never writes to a buffer that is in PLAYING state.
//     It only writes to EMPTY buffers → sets LOADING → then READY.
//     The audio thread reads PLAYING state buffers and transitions them to
//     EMPTY on buffer switch. The acquire/release pairs on stateA/B ensure
//     the data write is visible before the state flag is.
//
// ─────────────────────────────────────────────────────────────────────────────
//
// DESIGN NOTES:
// - Structs here must be POD-friendly or at least trivially copyable where
//   they are shared between threads (audio callback vs control thread).
// - The audio callback thread must NEVER allocate, lock, or do I/O.
//   Any struct read by the audio thread should be accessed via atomic pointers,
//   double-buffering, or lock-free queues — that coordination is handled by
//   the agents themselves, not by these types.
//
// PROVENANCE:
// - SpeakerLayoutData, SpeakerData, subwooferData → reused directly from
//   spatial_engine/src/LayoutLoader.hpp (shared via include path)
// - SpatialData, Keyframe, TimeUnit → reused directly from
//   spatial_engine/src/JSONLoader.hpp (shared via include path)
// - MonoWavData, MultiWavData → reused directly from
//   spatial_engine/src/WavUtils.hpp (shared via include path)
//
// This file defines ONLY the additional types needed for real-time operation
// that don't exist in the offline renderer's headers.

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// ElevationMode — Elevation handling for directions outside speaker coverage
// ─────────────────────────────────────────────────────────────────────────────
// Replicated from SpatialRenderer.hpp so the real-time engine doesn't depend
// on the offline renderer headers. Must stay in sync.
enum class ElevationMode {
    Clamp,              // Hard clip elevation to layout bounds
    RescaleAtmosUp,     // Default. Assumes content in [0, +π/2]. Maps to layout range.
    RescaleFullSphere   // Assumes content in [-π/2, +π/2]. Maps to layout range.
};

// ─────────────────────────────────────────────────────────────────────────────
// RealtimeConfig — Global configuration for the real-time engine
// ─────────────────────────────────────────────────────────────────────────────
// Set once at startup, read-only during playback except for the fields marked
// std::atomic, which may be written by the OSC listener thread at any time.
// The audio thread reads all atomics via relaxed loads in processBlock() Step A
// (snapshot) and in Pose::computePositions() (elevationMode). It NEVER writes
// back to any of these atomics — those are the exclusive domain of the writers.

struct RealtimeConfig {
    // ── Audio device settings ────────────────────────────────────────────
    int    sampleRate       = 48000;   // Audio sample rate in Hz
    int    bufferSize       = 512;     // Frames per audio callback buffer
    int    inputChannels    = 0;       // Input channels (0 = output only)

    // ── Layout-derived channel count ─────────────────────────────────────
    // COMPUTED from the speaker layout at load time — never set by the user.
    // Formula (matches offline SpatialRenderer.cpp):
    //   maxChannel = max(numSpeakers - 1, max(subwooferDeviceChannels))
    //   outputChannels = maxChannel + 1
    // This means the output buffer may contain gap channels (e.g., Allosphere
    // channels 13-16, 47-48 are unused). A future Channel Remap agent will
    // map these logical render channels to physical device outputs.
    // For now, AudioIO opens with this many channels (identity mapping).
    int    outputChannels   = 0;       // Set by Spatializer::init() from layout

    // ── Spatializer settings (mirrors offline RenderConfig) ──────────────
    // dbapFocus: atomic<float> so the OSC listener thread can safely write it
    // while the audio thread snapshots it in processBlock() Step A.
    // (Was a plain float before — that was a data race, even if benign on
    // most architectures. Fixed here for correctness.)
    std::atomic<float> dbapFocus{1.0f};  // DBAP focus/rolloff exponent (0.2–5.0)

    // Elevation rescaling mode — stored as atomic<int> so the OSC listener
    // thread can safely update it while the audio thread reads it per-block.
    // Cast to/from ElevationMode using static_cast<ElevationMode>(value).
    //   0 = RescaleAtmosUp (default)
    //   1 = RescaleFullSphere
    //   2 = Clamp
    std::atomic<int> elevationMode{
        static_cast<int>(ElevationMode::RescaleAtmosUp)};  // Runtime-switchable

    // ── Gain settings ────────────────────────────────────────────────────
    // All gain controls are atomic<float> / atomic<bool> so the GUI/control
    // thread can update them while the audio thread reads them without a lock.
    // Read ordering on the audio thread is relaxed — a one-buffer lag on a
    // gain change is inaudible and does not constitute a data race.
    // (Phase 6 — Compensation and Gain Agent)
    std::atomic<float> masterGain{0.5f};          // Global output gain (0.0–1.0)
    std::atomic<float> loudspeakerMix{1.0f};      // post-DBAP main-channel trim (±10 dB)
    std::atomic<float> subMix{1.0f};              // post-DBAP sub-channel trim  (±10 dB)
    std::atomic<bool>  focusAutoCompensation{false}; // auto-update loudspeakerMix on focus change
                                                     // NOTE: computeFocusCompensation() writes this
                                                     // from the MAIN thread only (see invariant 5)

    // ── File paths (set at startup, read-only after) ─────────────────────
    std::string layoutPath;       // Speaker layout JSON
    std::string scenePath;        // LUSID scene JSON (positions/trajectories)
    std::string sourcesFolder;    // Folder containing mono source WAV files
    std::string admFile;          // Multichannel ADM WAV file (direct streaming)
                                  // If non-empty, use ADM direct mode instead of
                                  // mono sources folder. Mutually exclusive with
                                  // sourcesFolder.

    // ── Playback control ─────────────────────────────────────────────────
    std::atomic<bool> playing{false};    // True when audio should be output
    std::atomic<bool> shouldExit{false}; // True when engine should shut down

    // ── Pause control (Phase 10 — GUI Agent) ─────────────────────────────
    // Written by the ParameterServer listener thread (relaxed store) when the
    // GUI sends /realtime/paused 1.0 or 0.0.
    // Read by the AUDIO thread (relaxed load) at the top of processBlock().
    // When true, processBlock() outputs silence and returns immediately.
    // Stale-by-one-buffer is fine — same contract as playing/masterGain.
    std::atomic<bool> paused{false};     // True = audio callback outputs silence
};


// ─────────────────────────────────────────────────────────────────────────────
// EngineState — Runtime state visible to all agents (read-mostly)
// ─────────────────────────────────────────────────────────────────────────────
// This struct is updated by the audio thread and read by the GUI/control
// thread for monitoring. All fields are atomic for safe cross-thread reads.
// This is intentionally minimal for Phase 1 — future phases add per-source
// and per-channel metrics.

struct EngineState {
    // ── Playback position ────────────────────────────────────────────────
    std::atomic<uint64_t> frameCounter{0};  // Current playback position in samples
    std::atomic<double>   playbackTimeSec{0.0}; // Current playback time in seconds

    // ── Performance monitoring ───────────────────────────────────────────
    std::atomic<float>    cpuLoad{0.0f};    // Audio thread CPU usage (0.0–1.0)
    std::atomic<uint64_t> xrunCount{0};     // Buffer underrun count

    // ── Phase 11: diagnostic counters ────────────────────────────────────
    // nanGuardCount: incremented once per audio block in which the post-render
    //   clamp pass in Spatializer::renderBlock() found a NaN, Inf, or sample
    //   outside [-4.0f, +4.0f]. Non-zero = DBAP distance/position bug.
    //   Written by the audio thread (sole writer); read by main thread for display.
    std::atomic<uint64_t> nanGuardCount{0};

    // speakerProximityCount: incremented each time the per-speaker minimum-
    //   distance guard in Spatializer::renderBlock() fires — i.e. a source
    //   position was within kMinSpeakerDist of a speaker and was pushed away.
    //   Non-zero values indicate trajectory segments that would otherwise
    //   produce DBAP gain spikes and audible clicks.
    //   Written by the audio thread (sole writer); read by main thread for display.
    std::atomic<uint64_t> speakerProximityCount{0};

    // ── Scene info (set once at load time) ───────────────────────────────
    std::atomic<int>      numSources{0};    // Number of active audio sources
    std::atomic<int>      numSpeakers{0};   // Number of speakers in layout
    std::atomic<double>   sceneDuration{0.0}; // Total scene duration in seconds
};

