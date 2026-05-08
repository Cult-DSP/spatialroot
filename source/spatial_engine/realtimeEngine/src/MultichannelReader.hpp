// MultichannelReader.hpp — Shared Multichannel WAV Reader for ADM Direct Streaming
//
// Opens a single multichannel ADM WAV file and reads interleaved chunks,
// de-interleaving individual channels into per-source SourceStream buffers.
//
// This avoids the Python stem-splitting step entirely: instead of N mono
// files on disk, we read from one multichannel file and extract channels
// on the C++ loader thread.
//
// DESIGN:
// - ONE SNDFILE* handle for the entire multichannel file.
// - ONE interleaved temp buffer (chunkFrames × numChannels floats).
// - A channel→SourceStream* map to route de-interleaved data.
// - readAndDistribute() is called by the Streaming loader thread.
//
// MEMORY:
// - Interleaved buffer: 240,000 frames × 48 ch × 4 bytes = ~44 MB
//   (allocated once, reused every chunk cycle).
//
// REAL-TIME SAFETY:
// - This class is ONLY used by the loader thread (never the audio thread).
// - The audio thread reads from the same SourceStream double-buffers as
//   in mono mode — completely unchanged and lock-free.
//
// PROVENANCE:
// - Factored out of Streaming.hpp to keep the mono path untouched.
// - De-interleave pattern is standard libsndfile usage.

#pragma once

#include <atomic>
#include <cstring>    // memset
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <sndfile.h>  // via Gamma (AlloLib external)

// Forward declaration — full definition in Streaming.hpp
struct SourceStream;

// ─────────────────────────────────────────────────────────────────────────────
// MultichannelReader
// ─────────────────────────────────────────────────────────────────────────────

class MultichannelReader {
public:

    MultichannelReader() = default;
    ~MultichannelReader() { close(); }

    // Non-copyable (owns SNDFILE handle)
    MultichannelReader(const MultichannelReader&) = delete;
    MultichannelReader& operator=(const MultichannelReader&) = delete;

    /// Open the multichannel WAV file. Call once at load time.
    /// Returns true on success.
    bool open(const std::string& path, int expectedSR, uint64_t chunkFrames) {
        mFilePath = path;
        mChunkFrames = chunkFrames;

        // Open file
        mSfInfo = {};
        mSndFile = sf_open(path.c_str(), SFM_READ, &mSfInfo);
        if (!mSndFile) {
            std::cerr << "[MultichannelReader] ERROR: Cannot open WAV: " << path
                      << " — " << sf_strerror(nullptr) << std::endl;
            return false;
        }

        mNumChannels = mSfInfo.channels;
        mTotalFrames = static_cast<uint64_t>(mSfInfo.frames);
        mSampleRate  = mSfInfo.samplerate;

        if (mNumChannels < 2) {
            std::cerr << "[MultichannelReader] WARNING: File has only "
                      << mNumChannels << " channel(s). Use mono mode instead."
                      << std::endl;
            sf_close(mSndFile);
            mSndFile = nullptr;
            return false;
        }

        if (mSampleRate != expectedSR) {
            std::cerr << "[MultichannelReader] ERROR: Sample rate mismatch in "
                      << path << " (got " << mSampleRate << ", expected "
                      << expectedSR << ")" << std::endl;
            sf_close(mSndFile);
            mSndFile = nullptr;
            return false;
        }

        // Pre-allocate the interleaved read buffer (one chunk's worth)
        // Size = chunkFrames × numChannels floats
        mInterleavedBuffer.resize(mChunkFrames * mNumChannels, 0.0f);

        std::cout << "[MultichannelReader] Opened: " << path << std::endl;
        std::cout << "  Channels:     " << mNumChannels << std::endl;
        std::cout << "  Total frames: " << mTotalFrames
                  << " (" << (double)mTotalFrames / mSampleRate << "s)"
                  << std::endl;
        std::cout << "  Interleaved buffer: "
                  << (mInterleavedBuffer.size() * sizeof(float) / (1024*1024))
                  << " MB" << std::endl;

        return true;
    }

    /// Register a SourceStream to receive data from a specific channel.
    /// channelIndex is 0-based (ADM channel 1 = index 0).
    void mapChannel(int channelIndex, SourceStream* stream) {
        if (channelIndex < 0 || channelIndex >= mNumChannels) {
            std::cerr << "[MultichannelReader] WARNING: Channel index "
                      << channelIndex << " out of range (0-"
                      << mNumChannels - 1 << ")" << std::endl;
            return;
        }
        mChannelMap[channelIndex] = stream;
    }

    /// Read an interleaved chunk starting at fileFrame and de-interleave
    /// into each mapped SourceStream's specified buffer.
    ///
    /// bufIdx: which buffer (0=A, 1=B) to write into on each SourceStream.
    /// Called ONLY by the Streaming loader thread.
    ///
    /// Returns the number of frames actually read (may be < chunkFrames at EOF).
    uint64_t readAndDistribute(uint64_t fileFrame, int bufIdx) {
        if (!mSndFile) return 0;

        // Clamp to file end
        uint64_t framesToRead = mChunkFrames;
        if (fileFrame + framesToRead > mTotalFrames) {
            framesToRead = (fileFrame < mTotalFrames) ? (mTotalFrames - fileFrame) : 0;
        }

        if (framesToRead == 0) {
            // Past end of file — zero-fill all mapped streams' buffers
            for (auto& [ch, stream] : mChannelMap) {
                zeroFillBuffer(stream, bufIdx, fileFrame);
            }
            return 0;
        }

        sf_count_t framesRead = 0;
        {
            std::lock_guard<std::mutex> lock(mFileMutex);
            sf_seek(mSndFile, static_cast<sf_count_t>(fileFrame), SEEK_SET);
            framesRead = sf_readf_float(mSndFile, mInterleavedBuffer.data(),
                                         static_cast<sf_count_t>(framesToRead));
        }

        if (framesRead <= 0) {
            for (auto& [ch, stream] : mChannelMap) {
                zeroFillBuffer(stream, bufIdx, fileFrame);
            }
            return 0;
        }

        // De-interleave: extract each mapped channel into its SourceStream buffer
        for (auto& [chIdx, stream] : mChannelMap) {
            deinterleaveInto(stream, bufIdx, chIdx,
                             static_cast<uint64_t>(framesRead), fileFrame);
        }

        return static_cast<uint64_t>(framesRead);
    }

    /// Read the first chunk (frame 0) into buffer A of all mapped streams.
    /// Called once at load time, before playback starts.
    bool readFirstChunk() {
        uint64_t read = readAndDistribute(0, 0);  // bufIdx=0 → buffer A
        return (read > 0);
    }

    /// Get the total frames in the multichannel file.
    uint64_t totalFrames() const { return mTotalFrames; }

    /// Get the number of channels in the file.
    int numChannels() const { return mNumChannels; }

    /// Get the number of mapped source streams.
    size_t numMappedChannels() const { return mChannelMap.size(); }

    /// Get the chunk size in frames.
    uint64_t chunkFrames() const { return mChunkFrames; }

    /// Close the file handle.
    void close() {
        if (mSndFile) {
            sf_close(mSndFile);
            mSndFile = nullptr;
        }
        mChannelMap.clear();
        mInterleavedBuffer.clear();
    }

private:

    /// De-interleave one channel from the interleaved buffer into a
    /// SourceStream's double buffer (A or B).
    /// This writes directly into the SourceStream's buffer and updates its
    /// atomic state flags — matching the contract of SourceStream::loadChunkInto().
    ///
    /// NOTE: Implementation is provided AFTER SourceStream is fully defined
    ///       (see bottom of Streaming.hpp). This is standard C++ practice for
    ///       breaking circular header dependencies.
    inline void deinterleaveInto(SourceStream* stream, int bufIdx, int channelIndex,
                                 uint64_t framesRead, uint64_t fileFrame);

    /// Zero-fill a SourceStream's buffer and mark it ready (past EOF).
    /// Implementation in Streaming.hpp (same reason as above).
    inline void zeroFillBuffer(SourceStream* stream, int bufIdx, uint64_t fileFrame);

    // ── Member data ──────────────────────────────────────────────────────

    SNDFILE*    mSndFile = nullptr;
    SF_INFO     mSfInfo  = {};
    std::mutex  mFileMutex;      // Protects sf_seek/sf_readf_float

    std::string mFilePath;
    int         mNumChannels = 0;
    uint64_t    mTotalFrames = 0;
    int         mSampleRate  = 0;
    uint64_t    mChunkFrames = 0;

    // Interleaved read buffer: chunkFrames × numChannels floats.
    // Allocated once at open(), reused for every chunk read.
    std::vector<float> mInterleavedBuffer;

    // Map: 0-based channel index → SourceStream that receives that channel's data.
    // Not all channels need to be mapped (empty channels are skipped).
    std::map<int, SourceStream*> mChannelMap;
};

