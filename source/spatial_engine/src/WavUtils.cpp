#include "WavUtils.hpp"
#include <sndfile.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

MonoWavData loadMonoFile(const fs::path &path) {
    SF_INFO info;
    SNDFILE *snd = sf_open(path.string().c_str(), SFM_READ, &info);
    if (!snd) throw std::runtime_error("Failed to open WAV: " + path.string());

    if (info.channels != 1)
        throw std::runtime_error("Source WAV is not mono: " + path.string());

    MonoWavData d;
    d.sampleRate = info.samplerate;
    d.samples.resize(info.frames);

    sf_read_float(snd, d.samples.data(), info.frames);
    sf_close(snd);

    return d;
}

std::map<std::string, MonoWavData>
WavUtils::loadSources(const std::string &folder,
                      const std::map<std::string, std::vector<struct Keyframe>> &sourceKeys,
                      int expectedSR)
{
    std::map<std::string, MonoWavData> out;

    for (auto &[name, kf] : sourceKeys) {
        fs::path p = fs::path(folder) / (name + ".wav");

        if (!fs::exists(p)) {
            throw std::runtime_error("Missing source WAV: " + p.string());
        }

        MonoWavData d = loadMonoFile(p);

        if (d.sampleRate != expectedSR) {
            throw std::runtime_error("Sample rate mismatch in: " + p.string());
        }

        out[name] = d;
    }

    return out;
}

// Parse source name to ADM channel index (0-based)
// "1.1" → 0, "2.1" → 1, "LFE" → 3 (if >= 4 channels)
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

std::map<std::string, MonoWavData>
WavUtils::loadSourcesFromADM(const std::string &admFile,
                             const std::map<std::string, std::vector<struct Keyframe>> &sourceKeys,
                             int expectedSR)
{
    std::map<std::string, MonoWavData> out;

    // Open the ADM file
    SF_INFO info;
    SNDFILE *snd = sf_open(admFile.c_str(), SFM_READ, &info);
    if (!snd) {
        throw std::runtime_error("Failed to open ADM WAV: " + admFile);
    }

    if (info.samplerate != expectedSR) {
        sf_close(snd);
        throw std::runtime_error("Sample rate mismatch in ADM file: " + admFile);
    }

    if (info.channels < 2) {
        sf_close(snd);
        throw std::runtime_error("ADM file must have at least 2 channels: " + admFile);
    }

    // Read all frames from the multichannel file
    std::vector<float> interleavedBuffer(info.frames * info.channels);
    sf_count_t framesRead = sf_readf_float(snd, interleavedBuffer.data(), info.frames);
    sf_close(snd);

    if (framesRead != info.frames) {
        throw std::runtime_error("Failed to read all frames from ADM file: " + admFile);
    }

    // Extract channels for each source
    for (auto &[name, kf] : sourceKeys) {
        int channelIndex = parseChannelIndex(name, info.channels);
        if (channelIndex < 0) {
            std::cerr << "Warning: Cannot map source '" << name << "' to ADM channel — skipping\n";
            continue;
        }

        MonoWavData d;
        d.sampleRate = info.samplerate;
        d.samples.resize(info.frames);

        // De-interleave: extract the specific channel
        for (sf_count_t frame = 0; frame < info.frames; ++frame) {
            d.samples[frame] = interleavedBuffer[frame * info.channels + channelIndex];
        }

        out[name] = d;
        std::cout << "  ✓ " << name << " → ADM ch " << (channelIndex + 1) << "\n";
    }

    return out;
}

void WavUtils::writeMultichannelWav(const std::string &path,
                                    const MultiWavData &mw)
{
    SF_INFO info = {};
    info.channels = mw.channels;
    info.samplerate = mw.sampleRate;

    // Auto-select RF64 when audio data exceeds the standard WAV 4 GB limit.
    // Standard WAV uses unsigned 32-bit data-chunk sizes (max ~4.29 GB).
    // RF64 (EBU Tech 3306) is the broadcast-standard extension with 64-bit sizes.
    // libsndfile supports RF64 natively — readers that support RF64 include
    // libsndfile, ffmpeg, SoX, Audacity, Reaper, and most DAWs.
    size_t dataSizeBytes = (size_t)mw.samples[0].size() * mw.channels * sizeof(float);
    constexpr size_t kWavMaxBytes = 0xFFFFFFFF;  // ~4.29 GB unsigned 32-bit limit

    if (dataSizeBytes > kWavMaxBytes) {
        info.format = SF_FORMAT_RF64 | SF_FORMAT_FLOAT;
        std::cout << "NOTE: Using RF64 format (data size "
                  << dataSizeBytes / (1024 * 1024) << " MB exceeds WAV 4 GB limit)\n";
    } else {
        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    }

    double durationSec = (double)mw.samples[0].size() / mw.sampleRate;
    std::cout << "Writing " << (dataSizeBytes > kWavMaxBytes ? "RF64" : "WAV")
              << ": " << mw.channels << " channels, "
              << mw.sampleRate << " Hz, "
              << durationSec << " seconds ("
              << mw.samples[0].size() << " samples/ch, "
              << dataSizeBytes / (1024 * 1024) << " MB)\n";

    SNDFILE *snd = sf_open(path.c_str(), SFM_WRITE, &info);
    if (!snd) {
        std::cerr << "Error opening file for write: " << sf_strerror(nullptr) << "\n";
        throw std::runtime_error("Cannot create WAV file");
    }

    size_t totalSamples = mw.samples[0].size();
    std::vector<float> interleaved(totalSamples * mw.channels);

    std::cout << "Interleaving " << interleaved.size() << " total samples...\n";

    for (size_t i = 0; i < totalSamples; i++) {
        for (int ch = 0; ch < mw.channels; ch++) {
            interleaved[i * mw.channels + ch] = mw.samples[ch][i];
        }
    }

    std::cout << "Writing to file...\n";
    sf_count_t written = sf_write_float(snd, interleaved.data(), interleaved.size());
    std::cout << "Wrote " << written << " samples (expected " << interleaved.size() << ")\n";
    
    if (written != (sf_count_t)interleaved.size()) {
        std::cerr << "Write error: " << sf_strerror(snd) << "\n";
    }
    
    sf_close(snd);
    std::cout << "File closed\n";
}
