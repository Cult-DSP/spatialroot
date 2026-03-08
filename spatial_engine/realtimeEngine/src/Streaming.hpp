// Streaming.hpp — Agent 1: Audio Streaming from Disk
//
// Streams mono WAV source files from disk in real-time using double-buffered
// I/O. Each source gets two pre-allocated buffers that alternate: one is read
// by the audio thread while the other is filled by a background loader thread.
//
// RESPONSIBILITIES:
// 1. Open all source WAV files referenced in the LUSID scene.
// 2. Pre-allocate double buffers for each source at load time.
// 3. Run a background thread that reads ahead into the inactive buffer.
// 4. Provide a lock-free getSamples() method for the audio callback.
// 5. Handle end-of-file (output silence after source ends).
//
// ─────────────────────────────────────────────────────────────────────────────
// THREADING MODEL (Phase 8 — Threading and Safety)
// ─────────────────────────────────────────────────────────────────────────────
//
// Three threads interact with Streaming:
//
//  MAIN thread:
//    - Calls loadScene() / loadSceneFromADM() (setup, before start())
//    - Calls startLoader() (before start())
//    - Calls shutdown() (ONLY after Backend::stop() has returned)
//    Owns: mStreams map lifetime, mMultichannelReader lifetime,
//          mLoaderRunning write (false → stops loader)
//
//  AUDIO thread:
//    - Calls getSample() / getBlock() on every audio callback
//    - NEVER holds a lock, never accesses SNDFILE*
//    Reads: SourceStream::bufferA/B, stateA/B (acquire), chunkStartA/B,
//           validFramesA/B, activeBuffer (acquire)
//    Writes: activeBuffer (release), stateA/B (release, on buffer switch only)
//
//  LOADER thread:
//    - Runs loaderWorker() in background
//    - Holds fileMutex only while calling libsndfile (sf_seek / sf_readf_float)
//    - Reads mState.frameCounter (relaxed) to check playback position
//    Reads:  stateA/B, activeBuffer (acquire) to decide which buf to fill
//    Writes: bufferA/B data, then chunkStart/validFrames (release),
//            then state (EMPTY→LOADING→READY) (release)
//
// MEMORY ORDERING (double-buffer acquire/release protocol):
//
//  Loader writes:
//    1. state ← LOADING  (release)    — marks buffer in-flight
//    2. buffer data written            — normal store (no ordering needed;
//                                         visibility guaranteed by step 3)
//    3. chunkStart ← N   (release)    — publish start position
//    4. validFrames ← F  (release)    — publish frame count
//    5. state ← READY    (release)    — final visibility fence;
//                                        audio thread may now use buffer
//
//  Audio reads in getSample() / getBlock():
//    - All loads use memory_order_acquire, which synchronizes with the
//      corresponding release stores above. When the audio thread sees
//      state == READY, it is guaranteed to also see the data written in
//      steps 2–4.
//
// SHUTDOWN ORDERING CONTRACT (INVARIANT — must never be violated):
//
//  The correct shutdown sequence is:
//    1. backend.stop()         — waits for the audio stream to stop;
//                                after this returns, no more audio callbacks
//                                will fire and getSample()/getBlock() will
//                                never be called again.
//    2. streaming.shutdown()   — sets mLoaderRunning=false, joins loader
//                                thread, then clears mStreams.
//                                Safe because the audio thread is already done.
//
//  If shutdown() were called BEFORE stop(), the audio thread could be inside
//  getSample() while mStreams is being cleared → use-after-free crash.
//  See main.cpp for the correct ordering.
//
// ─────────────────────────────────────────────────────────────────────────────
//
// REAL-TIME SAFETY:
// - The audio callback (getSamples) NEVER does file I/O, locks, or allocates.
// - It reads from a pre-filled buffer and uses atomic state flags.
// - The background loader thread is the ONLY thread that touches libsndfile.
// - A mutex protects SNDFILE* access (only used by the loader thread).
//
// DEPENDENCY:
// - <sndfile.h> comes transitively through Gamma (AlloLib external).
//   Gamma's CMake does: find_package(LibSndFile QUIET) and exports via PUBLIC.
//   Same API that spatial_engine/src/WavUtils.cpp already uses.
//   No new dependencies introduced.
//
// PROVENANCE:
// - Double-buffer pattern adapted from mainplayer.hpp prototype
//   (internalDocsMD/realtime_planning/mainplayer.hpp)
// - File loading pattern adapted from WavUtils.cpp (spatial_engine/src/)
// - Design follows realtimeEngine_designDoc.md §Streaming Module

#pragma once

#include <atomic>
#include <cstring>    // memset, memcpy
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>

#include <sndfile.h>  // via Gamma (AlloLib external) — NOT a new dependency

#include "RealtimeTypes.hpp"
#include "JSONLoader.hpp"  // SpatialData, Keyframe — shared from spatial_engine/src/
#include "MultichannelReader.hpp"  // ADM direct streaming — multichannel reader

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

// Chunk size in frames for each double buffer.
// Each buffer holds this many mono float samples.
// Phase 11: raised from 5 s → 10 s at 48kHz = 480,000 frames.
// Memory cost: ~1.8 MB → ~3.7 MB per source. For 80 sources: ~150 MB total
// (2 buffers × 80 sources × ~960 KB). Acceptable on a DAW-class workstation.
// Rationale: the loader needs to fill 10 s of audio while the audio thread
// consumes the last 25% of the active 10 s buffer (= 2.5 s).  At 48kHz mono
// float that is 192 KB/s × 80 sources = ~15 MB/s — well within any SSD.
static constexpr uint64_t kDefaultChunkFrames = 48000 * 10;  // 10 seconds

// When playback reaches this fraction of the current chunk, trigger preload
// of the next chunk into the inactive buffer.
// Phase 11: raised from 0.50 → 0.75. With 10 s chunks the loader now has
// 7.5 s to fill the next 10 s — a 0.75× real-time I/O rate requirement.
// Combined with the 10 s chunk size this makes buffer misses structurally
// impossible under normal operating conditions (see Invariant 9).
static constexpr float kPreloadThreshold = 0.75f;  // Start loading at 75%

// Per-sample fade multiplier applied on a buffer miss (fallback safety net).
// Phase 11: instead of returning hard 0.0f on underrun, the last sample is
// returned scaled by this factor per sample. Time constant ≈ 5 ms at 48 kHz:
//   exp(-1/(48000*0.005)) ≈ 0.99583 → effectively 0.9958^N reaches -60 dB
//   in ~N=2200 samples ≈ 45 ms. Chosen to mask any residual miss audibly
//   without masking the dropout entirely (a non-zero underrunCount is the
//   correct diagnostic signal).
static constexpr float kMissFadeRate = 0.9958f;

// ─────────────────────────────────────────────────────────────────────────────
// BufferState — State machine for each double buffer slot
// ─────────────────────────────────────────────────────────────────────────────
// Transitions:
//   EMPTY → LOADING (loader thread starts filling)
//   LOADING → READY (loader thread finished filling)
//   READY → PLAYING (audio thread switched to this buffer)
//   PLAYING → EMPTY (audio thread finished with this buffer, moved to other)

enum class StreamBufferState : int {
    EMPTY   = 0,  // Buffer is empty / available for loading
    LOADING = 1,  // Loader thread is filling this buffer
    READY   = 2,  // Buffer is filled and ready for audio thread
    PLAYING = 3   // Audio thread is currently reading from this buffer
};

// ─────────────────────────────────────────────────────────────────────────────
// SourceStream — Per-source streaming state
// ─────────────────────────────────────────────────────────────────────────────
// Each audio source (e.g., "1.1", "11.1", "LFE") gets one of these.
// Contains the SNDFILE handle, double buffers, and playback cursor.

struct SourceStream {
    // ── Identity ─────────────────────────────────────────────────────────
    std::string name;           // Source key (e.g., "1.1", "LFE")
    std::string filePath;       // Full path to the WAV file

    // ── File handle (only accessed by loader thread, protected by mutex) ─
    SNDFILE*    sndFile = nullptr;
    SF_INFO     sfInfo  = {};
    std::mutex  fileMutex;      // Protects sndFile seek/read operations

    // ── Double buffers ───────────────────────────────────────────────────
    // Two pre-allocated float buffers. Each holds up to chunkFrames samples.
    std::vector<float> bufferA;
    std::vector<float> bufferB;

    // Atomic state for each buffer (lock-free coordination)
    // Marked mutable because the audio thread may switch the active buffer
    // during a logically-const getSample() call (buffer switch doesn't
    // modify contents, just which buffer is "current").
    mutable std::atomic<StreamBufferState> stateA{StreamBufferState::EMPTY};
    mutable std::atomic<StreamBufferState> stateB{StreamBufferState::EMPTY};

    // The frame offset (in the source file) where each buffer's data starts
    std::atomic<uint64_t> chunkStartA{0};
    std::atomic<uint64_t> chunkStartB{0};

    // How many valid frames are actually in each buffer
    // (may be < chunkFrames for the last chunk in the file)
    std::atomic<uint64_t> validFramesA{0};
    std::atomic<uint64_t> validFramesB{0};

    // Which buffer is currently active for playback (0 = A, 1 = B)
    // Mutable for same reason as stateA/stateB above.
    mutable std::atomic<int> activeBuffer{-1};  // -1 = no buffer active yet

    // ── Playback state ───────────────────────────────────────────────────
    uint64_t totalFrames = 0;     // Total frames in the source WAV
    int      sampleRate  = 0;     // Source sample rate (must match engine)
    bool     isLFE       = false; // True if this is the LFE source

    // ── Buffer sizing ────────────────────────────────────────────────────
    uint64_t chunkFrames = kDefaultChunkFrames;

    // ── Phase 11: underrun state (audio-thread-owned, mutable for const getSample) ──
    // mFadeGain: envelope applied on a buffer miss (1.0 = normal, decays toward 0).
    //   Snapped back to 1.0 on any successful sample read. Starts a kMissFadeRate
    //   exponential decay per sample on miss. Makes residual dropouts audible as
    //   a short fade rather than a hard click. See Invariant 9.
    // underrunCount: incremented once per call to getSample() that ends in a miss
    //   (i.e., after the fade is already in progress OR on the first miss).
    //   Reported in the monitoring loop. Non-zero = pathological I/O condition.
    mutable float                    mFadeGain{1.0f};
    mutable std::atomic<uint64_t>    underrunCount{0};

    // ── Methods ──────────────────────────────────────────────────────────

    /// Open the WAV file and pre-allocate buffers. Called once at load time.
    /// Returns true on success.
    bool open(const std::string& path, const std::string& sourceName,
              uint64_t chunkSize, int expectedSR) {
        name = sourceName;
        filePath = path;
        chunkFrames = chunkSize;
        isLFE = (sourceName == "LFE");

        // Open the file (header only — no data loaded into memory)
        sfInfo = {};
        sndFile = sf_open(path.c_str(), SFM_READ, &sfInfo);
        if (!sndFile) {
            std::cerr << "[Streaming] ERROR: Cannot open WAV: " << path
                      << " — " << sf_strerror(nullptr) << std::endl;
            return false;
        }

        // Validate: must be mono
        if (sfInfo.channels != 1) {
            std::cerr << "[Streaming] ERROR: Source is not mono (" 
                      << sfInfo.channels << " ch): " << path << std::endl;
            sf_close(sndFile);
            sndFile = nullptr;
            return false;
        }

        // Validate: sample rate must match engine
        if (sfInfo.samplerate != expectedSR) {
            std::cerr << "[Streaming] ERROR: Sample rate mismatch in " << path
                      << " (got " << sfInfo.samplerate << ", expected " 
                      << expectedSR << ")" << std::endl;
            sf_close(sndFile);
            sndFile = nullptr;
            return false;
        }

        totalFrames = static_cast<uint64_t>(sfInfo.frames);
        sampleRate = sfInfo.samplerate;

        // Pre-allocate double buffers (no allocation during playback!)
        bufferA.resize(chunkFrames, 0.0f);
        bufferB.resize(chunkFrames, 0.0f);

        return true;
    }

    /// Initialize buffers WITHOUT opening a file handle. Used in multichannel
    /// (ADM direct) mode where MultichannelReader owns the file and fills
    /// these buffers via de-interleaving. The SourceStream still provides
    /// double-buffered playback to the audio thread exactly as in mono mode.
    bool initBuffersOnly(const std::string& sourceName, uint64_t chunkSize,
                         int sr, uint64_t frames) {
        name = sourceName;
        chunkFrames = chunkSize;
        isLFE = (sourceName == "LFE");
        totalFrames = frames;
        sampleRate = sr;

        // No file handle — MultichannelReader owns the SNDFILE*
        sndFile = nullptr;

        // Pre-allocate double buffers
        bufferA.resize(chunkFrames, 0.0f);
        bufferB.resize(chunkFrames, 0.0f);

        return true;
    }

    /// Load the first chunk synchronously into buffer A. Called once before
    /// playback starts (from the main thread, not the audio thread).
    bool loadFirstChunk() {
        if (!sndFile) return false;

        stateA.store(StreamBufferState::LOADING, std::memory_order_release);

        uint64_t framesToRead = std::min(chunkFrames, totalFrames);

        std::lock_guard<std::mutex> lock(fileMutex);
        sf_seek(sndFile, 0, SEEK_SET);
        sf_count_t read = sf_readf_float(sndFile, bufferA.data(),
                                          static_cast<sf_count_t>(framesToRead));

        if (read <= 0) {
            std::cerr << "[Streaming] ERROR: Failed to read first chunk for "
                      << name << std::endl;
            stateA.store(StreamBufferState::EMPTY, std::memory_order_release);
            return false;
        }

        // Zero-fill remainder if we read less than the chunk size
        if (static_cast<uint64_t>(read) < chunkFrames) {
            std::memset(bufferA.data() + read, 0,
                        (chunkFrames - read) * sizeof(float));
        }

        chunkStartA.store(0, std::memory_order_release);
        validFramesA.store(static_cast<uint64_t>(read), std::memory_order_release);
        stateA.store(StreamBufferState::READY, std::memory_order_release);

        // Activate buffer A for playback
        activeBuffer.store(0, std::memory_order_release);
        stateA.store(StreamBufferState::PLAYING, std::memory_order_release);

        return true;
    }

    /// Load a chunk starting at fileFrame into the specified buffer.
    /// Called ONLY by the loader thread.
    void loadChunkInto(int bufIdx, uint64_t fileFrame) {
        auto& buffer = (bufIdx == 0) ? bufferA : bufferB;
        auto& state  = (bufIdx == 0) ? stateA  : stateB;
        auto& start  = (bufIdx == 0) ? chunkStartA : chunkStartB;
        auto& valid  = (bufIdx == 0) ? validFramesA : validFramesB;

        state.store(StreamBufferState::LOADING, std::memory_order_release);

        // Clamp to file end
        uint64_t framesToRead = chunkFrames;
        if (fileFrame + framesToRead > totalFrames) {
            framesToRead = (fileFrame < totalFrames) ? (totalFrames - fileFrame) : 0;
        }

        if (framesToRead == 0) {
            // Past end of file — fill with silence
            std::memset(buffer.data(), 0, chunkFrames * sizeof(float));
            start.store(fileFrame, std::memory_order_release);
            valid.store(0, std::memory_order_release);
            state.store(StreamBufferState::READY, std::memory_order_release);
            return;
        }

        sf_count_t read = 0;
        {
            std::lock_guard<std::mutex> lock(fileMutex);
            sf_seek(sndFile, static_cast<sf_count_t>(fileFrame), SEEK_SET);
            read = sf_readf_float(sndFile, buffer.data(),
                                   static_cast<sf_count_t>(framesToRead));
        }

        // Zero-fill remainder
        if (static_cast<uint64_t>(read) < chunkFrames) {
            std::memset(buffer.data() + read, 0,
                        (chunkFrames - read) * sizeof(float));
        }

        start.store(fileFrame, std::memory_order_release);
        valid.store(static_cast<uint64_t>(read), std::memory_order_release);
        state.store(StreamBufferState::READY, std::memory_order_release);
    }

    /// Get the sample value at a given global frame position.
    /// Called ONLY from the audio callback thread — must be lock-free.
    ///
    /// Phase 11 — underrun handling (Invariant 9):
    ///   On a successful read, mFadeGain is snapped to 1.0f and the sample is
    ///   returned normally. On a miss (neither buffer has the frame), the last
    ///   returned sample is faded by kMissFadeRate per call and underrunCount
    ///   is incremented. This makes residual misses audible as a short fade
    ///   rather than a hard click, and makes them observable in the log.
    ///   Under normal operating conditions (10 s chunks, 75% threshold) this
    ///   path should NEVER be taken. A non-zero underrunCount is a bug signal.
    float getSample(uint64_t globalFrame) const {
        int active = activeBuffer.load(std::memory_order_acquire);
        if (active < 0) return 0.0f;  // No buffer active yet

        // Get active buffer's data
        const auto& buffer = (active == 0) ? bufferA : bufferB;
        uint64_t bufStart  = (active == 0)
            ? chunkStartA.load(std::memory_order_acquire)
            : chunkStartB.load(std::memory_order_acquire);
        uint64_t bufValid  = (active == 0)
            ? validFramesA.load(std::memory_order_acquire)
            : validFramesB.load(std::memory_order_acquire);

        // Check if the requested frame is within this buffer
        if (globalFrame >= bufStart && globalFrame < bufStart + bufValid) {
            mFadeGain = 1.0f;  // successful read — reset fade envelope
            return buffer[globalFrame - bufStart];
        }

        // Frame not in active buffer — check the other buffer
        int other = 1 - active;
        const auto& otherBuf = (other == 0) ? bufferA : bufferB;
        auto otherState = (other == 0)
            ? stateA.load(std::memory_order_acquire)
            : stateB.load(std::memory_order_acquire);
        uint64_t otherStart = (other == 0)
            ? chunkStartA.load(std::memory_order_acquire)
            : chunkStartB.load(std::memory_order_acquire);
        uint64_t otherValid = (other == 0)
            ? validFramesA.load(std::memory_order_acquire)
            : validFramesB.load(std::memory_order_acquire);

        if (otherState == StreamBufferState::READY &&
            globalFrame >= otherStart && globalFrame < otherStart + otherValid) {
            // The other buffer has our data — switch to it!
            // (This is a benign race: worst case two blocks both switch,
            //  but the data is consistent either way.)
            // stateA/stateB/activeBuffer are mutable, so this works in const.
            auto& mutState  = (active == 0) ? stateA : stateB;
            auto& othState  = (other == 0)  ? stateA : stateB;

            mutState.store(StreamBufferState::EMPTY, std::memory_order_release);
            othState.store(StreamBufferState::PLAYING, std::memory_order_release);
            activeBuffer.store(other, std::memory_order_release);

            mFadeGain = 1.0f;  // successful switch — reset fade envelope
            return otherBuf[globalFrame - otherStart];
        }

        // ── Underrun (Phase 11 fallback — should never fire under normal ops) ──
        // Neither buffer has the requested frame. Apply exponential fade-to-zero
        // rather than returning a hard 0.0f click. Increment counter for log.
        underrunCount.fetch_add(1, std::memory_order_relaxed);
        if (mFadeGain > 1e-4f) {
            mFadeGain *= kMissFadeRate;
        } else {
            mFadeGain = 0.0f;
        }
        // Return 0.0f once fully faded (no DC hold — avoids sustained buzz).
        return 0.0f;
    }

    /// Close the file handle. Called at shutdown.
    void close() {
        if (sndFile) {
            sf_close(sndFile);
            sndFile = nullptr;
        }
    }

    ~SourceStream() { close(); }

    // Non-copyable (owns SNDFILE handle)
    SourceStream() = default;
    SourceStream(const SourceStream&) = delete;
    SourceStream& operator=(const SourceStream&) = delete;
    SourceStream(SourceStream&& other) noexcept {
        name = std::move(other.name);
        filePath = std::move(other.filePath);
        sndFile = other.sndFile;  other.sndFile = nullptr;
        sfInfo = other.sfInfo;
        bufferA = std::move(other.bufferA);
        bufferB = std::move(other.bufferB);
        stateA.store(other.stateA.load());
        stateB.store(other.stateB.load());
        chunkStartA.store(other.chunkStartA.load());
        chunkStartB.store(other.chunkStartB.load());
        validFramesA.store(other.validFramesA.load());
        validFramesB.store(other.validFramesB.load());
        activeBuffer.store(other.activeBuffer.load());
        totalFrames = other.totalFrames;
        sampleRate = other.sampleRate;
        isLFE = other.isLFE;
        chunkFrames = other.chunkFrames;
        mFadeGain = other.mFadeGain;
        underrunCount.store(other.underrunCount.load());
    }
    SourceStream& operator=(SourceStream&& other) noexcept {
        if (this != &other) {
            close();
            name = std::move(other.name);
            filePath = std::move(other.filePath);
            sndFile = other.sndFile;  other.sndFile = nullptr;
            sfInfo = other.sfInfo;
            bufferA = std::move(other.bufferA);
            bufferB = std::move(other.bufferB);
            stateA.store(other.stateA.load());
            stateB.store(other.stateB.load());
            chunkStartA.store(other.chunkStartA.load());
            chunkStartB.store(other.chunkStartB.load());
            validFramesA.store(other.validFramesA.load());
            validFramesB.store(other.validFramesB.load());
            activeBuffer.store(other.activeBuffer.load());
            totalFrames = other.totalFrames;
            sampleRate = other.sampleRate;
            isLFE = other.isLFE;
            chunkFrames = other.chunkFrames;
            mFadeGain = other.mFadeGain;
            underrunCount.store(other.underrunCount.load());
        }
        return *this;
    }
};


// ─────────────────────────────────────────────────────────────────────────────
// Streaming — Manages all source streams and the background loader
// ─────────────────────────────────────────────────────────────────────────────

class Streaming {
public:

    Streaming(RealtimeConfig& config, EngineState& state)
        : mConfig(config), mState(state) {}

    ~Streaming() { shutdown(); }

    // ── Load all sources from a LUSID scene ──────────────────────────────
    // Opens each source WAV file and pre-loads the first chunk.
    // Must be called BEFORE starting the audio stream.
    //
    // The source name → filename convention follows WavUtils::loadSources():
    //   source key "1.1" → file "1.1.wav"
    //   source key "LFE" → file "LFE.wav"

    bool loadScene(const SpatialData& scene) {
        std::cout << "[Streaming] Loading " << scene.sources.size()
                  << " sources from: " << mConfig.sourcesFolder << std::endl;

        for (const auto& [sourceName, keyframes] : scene.sources) {
            // Build file path: sourcesFolder/sourceName.wav
            fs::path wavPath = fs::path(mConfig.sourcesFolder) / (sourceName + ".wav");

            if (!fs::exists(wavPath)) {
                std::cerr << "[Streaming] WARNING: Missing source WAV: "
                          << wavPath << " — skipping." << std::endl;
                continue;
            }

            // Create stream for this source
            auto stream = std::make_unique<SourceStream>();
            if (!stream->open(wavPath.string(), sourceName,
                              kDefaultChunkFrames, mConfig.sampleRate)) {
                std::cerr << "[Streaming] WARNING: Failed to open " 
                          << sourceName << " — skipping." << std::endl;
                continue;
            }

            // Load first chunk synchronously
            if (!stream->loadFirstChunk()) {
                std::cerr << "[Streaming] WARNING: Failed to preload "
                          << sourceName << " — skipping." << std::endl;
                continue;
            }

            std::cout << "  ✓ " << sourceName << " — "
                      << stream->totalFrames << " frames ("
                      << (double)stream->totalFrames / stream->sampleRate
                      << "s)" << (stream->isLFE ? " [LFE]" : "") << std::endl;

            mStreams[sourceName] = std::move(stream);
        }

        mState.numSources.store(static_cast<int>(mStreams.size()),
                                std::memory_order_relaxed);

        std::cout << "[Streaming] Loaded " << mStreams.size() << " sources."
                  << std::endl;

        return !mStreams.empty();
    }

    // ── Load all sources from a multichannel ADM WAV (direct streaming) ──
    // Instead of individual mono files, reads from one multichannel file
    // and de-interleaves channels into per-source buffers.
    //
    // Channel mapping convention (matches LUSID source key naming):
    //   "N.1" → ADM channel N → 0-based index (N-1)
    //   "LFE" → ADM channel 4 → 0-based index 3
    //
    // Only maps channels that appear in the scene's source list.

    bool loadSceneFromADM(const SpatialData& scene, const std::string& admFilePath) {
        std::cout << "[Streaming] Loading " << scene.sources.size()
                  << " sources from multichannel ADM: " << admFilePath << std::endl;

        mMultichannelMode = true;

        // Create the multichannel reader and open the ADM file
        mMultichannelReader = std::make_unique<MultichannelReader>();
        if (!mMultichannelReader->open(admFilePath, mConfig.sampleRate,
                                        kDefaultChunkFrames)) {
            std::cerr << "[Streaming] FATAL: Failed to open ADM file." << std::endl;
            return false;
        }

        uint64_t admTotalFrames = mMultichannelReader->totalFrames();
        int      admNumChannels = mMultichannelReader->numChannels();

        // Create buffer-only SourceStreams and map channels
        for (const auto& [sourceName, keyframes] : scene.sources) {
            // Parse source name to get 0-based channel index
            int channelIndex = parseChannelIndex(sourceName, admNumChannels);
            if (channelIndex < 0) {
                std::cerr << "[Streaming] WARNING: Cannot map source \""
                          << sourceName << "\" to ADM channel — skipping."
                          << std::endl;
                continue;
            }

            // Create a buffer-only stream (no individual file handle)
            auto stream = std::make_unique<SourceStream>();
            stream->initBuffersOnly(sourceName, kDefaultChunkFrames,
                                    mConfig.sampleRate, admTotalFrames);

            // Register with the multichannel reader
            mMultichannelReader->mapChannel(channelIndex, stream.get());

            std::cout << "  ✓ " << sourceName << " → ADM ch " << (channelIndex + 1)
                      << " (0-based: " << channelIndex << ")"
                      << (stream->isLFE ? " [LFE]" : "") << std::endl;

            mStreams[sourceName] = std::move(stream);
        }

        if (mStreams.empty()) {
            std::cerr << "[Streaming] FATAL: No sources could be mapped." << std::endl;
            return false;
        }

        // Read the first chunk from the multichannel file into buffer A
        // of all mapped streams (synchronous, before playback starts).
        if (!mMultichannelReader->readFirstChunk()) {
            std::cerr << "[Streaming] FATAL: Failed to read first chunk from ADM."
                      << std::endl;
            return false;
        }

        // Activate buffer A for playback on all streams
        for (auto& [name, stream] : mStreams) {
            stream->activeBuffer.store(0, std::memory_order_release);
            stream->stateA.store(StreamBufferState::PLAYING, std::memory_order_release);
        }

        mState.numSources.store(static_cast<int>(mStreams.size()),
                                std::memory_order_relaxed);

        std::cout << "[Streaming] Loaded " << mStreams.size() << " sources from ADM ("
                  << mMultichannelReader->numMappedChannels() << " of "
                  << admNumChannels << " channels mapped)." << std::endl;

        return true;
    }

    // ── Start the background loader thread ───────────────────────────────
    // Must be called AFTER loadScene() and BEFORE starting audio.

    void startLoader() {
        mLoaderRunning.store(true, std::memory_order_release);
        mLoaderThread = std::thread([this]() { loaderWorker(); });
        std::cout << "[Streaming] Background loader thread started." << std::endl;
    }

    // ── Get a sample for a given source at a global frame position ───────
    // Called from the audio callback — MUST be lock-free and real-time safe.

    float getSample(const std::string& sourceName, uint64_t globalFrame) const {
        auto it = mStreams.find(sourceName);
        if (it == mStreams.end()) return 0.0f;
        return it->second->getSample(globalFrame);
    }

    // ── Get a block of samples for a source into a pre-allocated buffer ──
    // More efficient than per-sample getSample() — copies a contiguous
    // block from the active buffer when possible.
    // Called from the audio callback — MUST be lock-free.

    void getBlock(const std::string& sourceName, uint64_t startFrame,
                  unsigned int numFrames, float* outBuffer) const {
        auto it = mStreams.find(sourceName);
        if (it == mStreams.end()) {
            std::memset(outBuffer, 0, numFrames * sizeof(float));
            return;
        }

        const SourceStream& src = *it->second;
        int active = src.activeBuffer.load(std::memory_order_acquire);

        if (active < 0) {
            std::memset(outBuffer, 0, numFrames * sizeof(float));
            return;
        }

        // Try to get the whole block from the active buffer
        const auto& buffer   = (active == 0) ? src.bufferA : src.bufferB;
        uint64_t bufStart    = (active == 0)
            ? src.chunkStartA.load(std::memory_order_acquire)
            : src.chunkStartB.load(std::memory_order_acquire);
        uint64_t bufValid    = (active == 0)
            ? src.validFramesA.load(std::memory_order_acquire)
            : src.validFramesB.load(std::memory_order_acquire);

        uint64_t endFrame = startFrame + numFrames;

        // Happy path: entire block fits in the active buffer
        if (startFrame >= bufStart && endFrame <= bufStart + bufValid) {
            std::memcpy(outBuffer, buffer.data() + (startFrame - bufStart),
                        numFrames * sizeof(float));
            return;
        }

        // Slow path: block spans a buffer boundary, or active buffer doesn't
        // have our data. Fall back to per-sample access (handles buffer switch).
        for (unsigned int i = 0; i < numFrames; ++i) {
            outBuffer[i] = src.getSample(startFrame + i);
        }
    }

    // ── Source queries ────────────────────────────────────────────────────

    /// Get the list of loaded source names.
    std::vector<std::string> sourceNames() const {
        std::vector<std::string> names;
        names.reserve(mStreams.size());
        for (const auto& [name, _] : mStreams) {
            names.push_back(name);
        }
        return names;
    }

    /// Check if a source is the LFE channel.
    bool isLFE(const std::string& sourceName) const {
        auto it = mStreams.find(sourceName);
        return (it != mStreams.end()) ? it->second->isLFE : false;
    }

    /// Get total frames for a source.
    uint64_t totalFrames(const std::string& sourceName) const {
        auto it = mStreams.find(sourceName);
        return (it != mStreams.end()) ? it->second->totalFrames : 0;
    }

    /// Number of loaded sources.
    size_t numSources() const { return mStreams.size(); }

    /// Phase 11: total underrun sample count across all sources.
    /// Each count represents one sample that was requested but not available.
    /// Called from the main thread monitoring loop (relaxed read is fine —
    /// display lag of one buffer is acceptable).
    /// Non-zero value = pathological I/O condition. See Invariant 9.
    uint64_t totalUnderruns() const {
        uint64_t total = 0;
        for (const auto& [name, stream] : mStreams) {
            total += stream->underrunCount.load(std::memory_order_relaxed);
        }
        return total;
    }

    // ── Shutdown ─────────────────────────────────────────────────────────
    // THREADING CONTRACT: Caller MUST call Backend::stop() and wait for the
    // audio stream to finish before calling this method.
    //
    // After Backend::stop() returns, the audio thread will never call
    // getSample() / getBlock() again. Only then is it safe to:
    //   (a) signal the loader thread to exit (mLoaderRunning = false)
    //   (b) join the loader thread (no more writes to buffer data)
    //   (c) destroy mStreams (no more readers of buffer memory)
    //
    // Violating this ordering → use-after-free on the audio thread.

    void shutdown() {
        // Stop loader thread first — sets the flag (release) and joins.
        // After join() returns, the loader thread has exited and will never
        // again write to any SourceStream buffer.
        mLoaderRunning.store(false, std::memory_order_release);
        if (mLoaderThread.joinable()) {
            mLoaderThread.join();
        }
        // Close multichannel reader if active
        if (mMultichannelReader) {
            mMultichannelReader->close();
            mMultichannelReader.reset();
        }
        mMultichannelMode = false;
        // Close all file handles and destroy stream objects.
        // Safe because: (1) audio thread is stopped (caller precondition),
        //               (2) loader thread is joined (just above).
        for (auto& [name, stream] : mStreams) {
            stream->close();
        }
        mStreams.clear();
        std::cout << "[Streaming] Shutdown complete." << std::endl;
    }


private:

    // ── Background loader thread ─────────────────────────────────────────
    // Runs continuously, checking each source to see if the inactive buffer
    // needs to be filled with the next chunk. Sleeps briefly between scans
    // to avoid burning CPU.

    void loaderWorker() {
        while (mLoaderRunning.load(std::memory_order_acquire)) {

            // Get current playback position from engine state
            uint64_t currentFrame = mState.frameCounter.load(std::memory_order_relaxed);

            if (mMultichannelMode && mMultichannelReader) {
                // ── Multichannel (ADM direct) mode ───────────────────────
                // All streams share the same file and chunk boundaries.
                // We check ANY stream's active buffer to decide when to
                // trigger a bulk read + distribute for all channels at once.
                loaderWorkerMultichannel(currentFrame);
            } else {
                // ── Mono file mode (original behavior) ───────────────────
                loaderWorkerMono(currentFrame);
            }

            // Sleep to avoid busy-waiting. 2ms is well under the audio buffer
            // period (~10ms at 512 frames/48kHz) but frequent enough to catch
            // preload triggers in time.
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    /// Mono mode loader: each source has its own file, load independently.
    void loaderWorkerMono(uint64_t currentFrame) {
        for (auto& [name, stream] : mStreams) {
            if (!stream->sndFile) continue;

            int active = stream->activeBuffer.load(std::memory_order_acquire);
            if (active < 0) continue;

            // Determine which buffer is active and which is inactive
            uint64_t activeStart = (active == 0)
                ? stream->chunkStartA.load(std::memory_order_acquire)
                : stream->chunkStartB.load(std::memory_order_acquire);
            uint64_t activeValid = (active == 0)
                ? stream->validFramesA.load(std::memory_order_acquire)
                : stream->validFramesB.load(std::memory_order_acquire);

            int inactive = 1 - active;
            auto inactiveState = (inactive == 0)
                ? stream->stateA.load(std::memory_order_acquire)
                : stream->stateB.load(std::memory_order_acquire);

            // Check if we've consumed enough of the active buffer to
            // warrant preloading the next chunk into the inactive buffer.
            // Trigger at kPreloadThreshold (50%) of the active chunk.
            if (activeValid > 0 && inactiveState == StreamBufferState::EMPTY) {
                uint64_t threshold = activeStart +
                    static_cast<uint64_t>(activeValid * kPreloadThreshold);

                if (currentFrame >= threshold) {
                    // Calculate the start of the next chunk
                    uint64_t nextChunkStart = activeStart + stream->chunkFrames;

                    // Don't load past the end of the file
                    if (nextChunkStart < stream->totalFrames) {
                        stream->loadChunkInto(inactive, nextChunkStart);
                    }
                }
            }
        }
    }

    /// Multichannel mode loader: one shared file, bulk read + de-interleave.
    /// All streams share chunk boundaries — check one representative stream
    /// to decide when to trigger the next bulk read.
    void loaderWorkerMultichannel(uint64_t currentFrame) {
        // Find a representative stream (first one) to check timing
        if (mStreams.empty()) return;
        auto& representative = mStreams.begin()->second;

        int active = representative->activeBuffer.load(std::memory_order_acquire);
        if (active < 0) return;

        uint64_t activeStart = (active == 0)
            ? representative->chunkStartA.load(std::memory_order_acquire)
            : representative->chunkStartB.load(std::memory_order_acquire);
        uint64_t activeValid = (active == 0)
            ? representative->validFramesA.load(std::memory_order_acquire)
            : representative->validFramesB.load(std::memory_order_acquire);

        int inactive = 1 - active;
        auto inactiveState = (inactive == 0)
            ? representative->stateA.load(std::memory_order_acquire)
            : representative->stateB.load(std::memory_order_acquire);

        // Same preload logic as mono mode, but applied to all channels at once
        if (activeValid > 0 && inactiveState == StreamBufferState::EMPTY) {
            uint64_t threshold = activeStart +
                static_cast<uint64_t>(activeValid * kPreloadThreshold);

            if (currentFrame >= threshold) {
                uint64_t nextChunkStart = activeStart + representative->chunkFrames;

                if (nextChunkStart < mMultichannelReader->totalFrames()) {
                    // One bulk read + de-interleave fills ALL mapped streams
                    mMultichannelReader->readAndDistribute(nextChunkStart, inactive);
                }
            }
        }
    }

    // ── Channel index parsing ────────────────────────────────────────────
    // Maps LUSID source key names to 0-based ADM channel indices.
    //
    // Convention:
    //   "N.1"  → ADM track N → 0-based index (N - 1)
    //     e.g., "1.1" → 0, "11.1" → 10, "24.1" → 23
    //   "LFE"  → ADM channel 4 → 0-based index 3
    //     (standard ADM bed layout: L=1, R=2, C=3, LFE=4, ...)

    static int parseChannelIndex(const std::string& sourceName, int numChannels) {
        // Handle LFE special case
        if (sourceName == "LFE") {
            return (numChannels >= 4) ? 3 : -1;
        }

        // Parse "N.1" pattern: extract the integer N before the dot
        size_t dotPos = sourceName.find('.');
        if (dotPos == std::string::npos || dotPos == 0) {
            return -1;  // No dot found or starts with dot
        }

        try {
            int trackNum = std::stoi(sourceName.substr(0, dotPos));
            int index = trackNum - 1;  // 1-based → 0-based
            if (index >= 0 && index < numChannels) {
                return index;
            }
        } catch (...) {
            // Not a valid integer prefix
        }

        return -1;
    }

    // ── Member data ──────────────────────────────────────────────────────

    RealtimeConfig& mConfig;
    EngineState&    mState;

    // All active source streams, keyed by source name (e.g., "1.1", "LFE")
    std::map<std::string, std::unique_ptr<SourceStream>> mStreams;

    // ── Multichannel (ADM direct) mode ───────────────────────────────────
    // When true, sources are read from one multichannel file via the reader,
    // not from individual mono files.
    bool mMultichannelMode = false;
    std::unique_ptr<MultichannelReader> mMultichannelReader;

    // Background loader thread
    std::thread          mLoaderThread;
    std::atomic<bool>    mLoaderRunning{false};
};


// ─────────────────────────────────────────────────────────────────────────────
// MultichannelReader method implementations
// ─────────────────────────────────────────────────────────────────────────────
// These are defined here (after SourceStream is fully defined) rather than in
// MultichannelReader.hpp, because they need access to SourceStream's members.
// This is standard C++ practice for breaking circular header dependencies.

inline void MultichannelReader::deinterleaveInto(
    SourceStream* stream, int bufIdx, int channelIndex,
    uint64_t framesRead, uint64_t fileFrame)
{
    auto& buffer = (bufIdx == 0) ? stream->bufferA : stream->bufferB;
    auto& state  = (bufIdx == 0) ? stream->stateA  : stream->stateB;
    auto& start  = (bufIdx == 0) ? stream->chunkStartA : stream->chunkStartB;
    auto& valid  = (bufIdx == 0) ? stream->validFramesA : stream->validFramesB;

    state.store(StreamBufferState::LOADING, std::memory_order_release);

    // De-interleave: extract this channel from the interleaved buffer.
    // Interleaved layout: [ch0_f0, ch1_f0, ..., chN_f0, ch0_f1, ch1_f1, ...]
    // For frame i, channel c: interleavedBuffer[i * numChannels + c]
    const float* src = mInterleavedBuffer.data();
    float* dst = buffer.data();

    for (uint64_t i = 0; i < framesRead; ++i) {
        dst[i] = src[i * mNumChannels + channelIndex];
    }

    // Zero-fill remainder if we read less than the chunk size
    if (framesRead < stream->chunkFrames) {
        std::memset(dst + framesRead, 0,
                    (stream->chunkFrames - framesRead) * sizeof(float));
    }

    start.store(fileFrame, std::memory_order_release);
    valid.store(framesRead, std::memory_order_release);
    state.store(StreamBufferState::READY, std::memory_order_release);
}

inline void MultichannelReader::zeroFillBuffer(
    SourceStream* stream, int bufIdx, uint64_t fileFrame)
{
    auto& buffer = (bufIdx == 0) ? stream->bufferA : stream->bufferB;
    auto& state  = (bufIdx == 0) ? stream->stateA  : stream->stateB;
    auto& start  = (bufIdx == 0) ? stream->chunkStartA : stream->chunkStartB;
    auto& valid  = (bufIdx == 0) ? stream->validFramesA : stream->validFramesB;

    state.store(StreamBufferState::LOADING, std::memory_order_release);
    std::memset(buffer.data(), 0, stream->chunkFrames * sizeof(float));
    start.store(fileFrame, std::memory_order_release);
    valid.store(0, std::memory_order_release);
    state.store(StreamBufferState::READY, std::memory_order_release);
}
