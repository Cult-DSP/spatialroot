// OutputRemap.hpp — Output Channel Routing Table
//
// Maps internal channel indices (compact, 0-based, DBAP-owned) to physical
// AudioIO output channel indices (sparse, layout-derived) at the end of each
// audio block in Spatializer::renderBlock() Phase 7.
//
// TWO CONSTRUCTION PATHS:
//   buildAuto() — primary path. Called by Spatializer::init() with entries
//     derived from the speaker layout JSON's deviceChannel fields. This is
//     the only supported public routing source. Entries are one-to-one and
//     validated before this call; out-of-range entries after validation are
//     an internal engine error, not a user error, and are treated as such.
//
//   load() — legacy CSV path. DEPRECATED. Retained temporarily as internal
//     scaffolding during layout-routing validation. Out-of-range CSV entries
//     are silently dropped (untrusted external input).
//
// IDENTITY FAST PATH:
//   When internalChannelCount == outputChannelCount and all entries are
//   diagonal (entry.layout == entry.device with full coverage), identity=true
//   is set and Spatializer uses a direct contiguous copy. checkIdentity()
//   enforces the width-equality condition explicitly.
//
// REAL-TIME SAFETY:
//   - No allocation, no file I/O, no locks in the audio path.
//   - Entries and identity flag are set once (buildAuto or load), then
//     read-only during playback. The audio thread holds a const pointer.

#pragma once

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// RemapEntry — one (layout, device) pair from the CSV
// ─────────────────────────────────────────────────────────────────────────────

struct RemapEntry {
    int layout;   // source channel in the render buffer (0-based)
    int device;   // destination channel in AudioIO output (0-based)
};

// ─────────────────────────────────────────────────────────────────────────────
// OutputRemap — the remap table loaded from a CSV
// ─────────────────────────────────────────────────────────────────────────────

class OutputRemap {
public:

    // Default constructor → identity mapping (no CSV).
    OutputRemap() = default;

    // ── Load from CSV (DEPRECATED — legacy scaffolding only) ─────────────
    // DEPRECATED: layout-derived routing via buildAuto() is the supported path.
    // This method is retained temporarily for internal validation only.
    // Will be removed after layout-routing validation is complete.
    //
    // Call once on the main thread before the audio callback starts.
    // Returns true on success (even if some rows were skipped).
    // On failure (file not found / no valid rows) falls back to identity.
    //
    // renderChannels: internal bus width (numSpeakers + numSubwoofers).
    //   Out-of-range `layout` entries (>= renderChannels) are dropped.
    // deviceChannels: output bus width (max deviceChannel + 1).
    //   Out-of-range `device` entries (>= deviceChannels) are dropped.
    bool load(const std::string& csvPath,
              int renderChannels,
              int deviceChannels) {
        mEntries.clear();
        mIdentity = true;
        mMaxDeviceIndex = -1;

        std::ifstream file(csvPath);
        if (!file.is_open()) {
            std::cerr << "[OutputRemap] WARNING: Could not open remap CSV: "
                      << csvPath << " — using identity mapping." << std::endl;
            return false;
        }

        // ── Find header line ─────────────────────────────────────────────
        // Accept case-insensitive "layout" and "device" columns anywhere in
        // the first non-comment, non-empty line.
        int layoutCol = -1;
        int deviceCol = -1;
        std::string line;
        while (std::getline(file, line)) {
            trim(line);
            if (line.empty() || line[0] == '#') continue;

            // Parse header
            std::vector<std::string> cols = splitCSV(line);
            for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
                std::string h = cols[i];
                toLower(h);
                if (h == "layout") layoutCol = i;
                else if (h == "device") deviceCol = i;
            }
            break;
        }

        if (layoutCol < 0 || deviceCol < 0) {
            std::cerr << "[OutputRemap] WARNING: CSV missing 'layout' or 'device' header in "
                      << csvPath << " — using identity mapping." << std::endl;
            return false;
        }

        // ── Parse data rows ──────────────────────────────────────────────
        int rowsRead    = 0;
        int rowsDropped = 0;

        while (std::getline(file, line)) {
            trim(line);
            if (line.empty() || line[0] == '#') continue;

            std::vector<std::string> cols = splitCSV(line);
            int maxCol = std::max(layoutCol, deviceCol);
            if (static_cast<int>(cols.size()) <= maxCol) {
                ++rowsDropped;
                continue;
            }

            int lay = -1, dev = -1;
            try {
                lay = std::stoi(cols[layoutCol]);
                dev = std::stoi(cols[deviceCol]);
            } catch (...) {
                ++rowsDropped;
                continue;
            }

            // Range checks
            if (lay < 0 || lay >= renderChannels) {
                ++rowsDropped;
                continue;
            }
            if (dev < 0 || dev >= deviceChannels) {
                ++rowsDropped;
                continue;
            }

            mEntries.push_back({lay, dev});
            if (dev > mMaxDeviceIndex) mMaxDeviceIndex = dev;
            ++rowsRead;
        }

        if (rowsDropped > 0) {
            std::cerr << "[OutputRemap] " << rowsDropped
                      << " row(s) dropped (out-of-range or malformed)." << std::endl;
        }

        if (rowsRead == 0) {
            std::cerr << "[OutputRemap] WARNING: No valid rows in "
                      << csvPath << " — using identity mapping." << std::endl;
            mIdentity = true;
            return false;
        }

        // ── Detect if this is a pure identity map ────────────────────────
        // True iff entries cover 0..N-1 consecutively with layout==device.
        mIdentity = checkIdentity(renderChannels, deviceChannels);

        std::cout << "[OutputRemap] Loaded " << rowsRead << " entries from "
                  << csvPath
                  << (mIdentity ? " (identity map — fast path active)" : " (non-identity remap)")
                  << std::endl;
        return true;
    }

    // ── Build from layout-derived entries (primary path) ─────────────────
    // Called once by Spatializer::init() on the main thread, before start().
    // internalChannels: width of the internal bus (numSpeakers + numSubwoofers).
    // outputChannels:   width of the physical output bus (max deviceChannel + 1).
    // Entries are produced from validated layout data — all values are in range
    // by construction. Out-of-range entries after validation are a hard engine
    // bug: log loudly and leave the table empty (triggers init() guard).
    void buildAuto(std::vector<RemapEntry> entries,
                   int internalChannels,
                   int outputChannels) {
        mEntries.clear();
        mMaxDeviceIndex = -1;
        for (const auto& e : entries) {
            if (e.layout < 0 || e.layout >= internalChannels) {
                std::cerr << "[OutputRouting] INTERNAL ERROR: out-of-range internal channel "
                          << e.layout << " (max " << internalChannels - 1 << ") — "
                          << "this is a bug in routing construction." << std::endl;
                mEntries.clear();
                return;
            }
            if (e.device < 0 || e.device >= outputChannels) {
                std::cerr << "[OutputRouting] INTERNAL ERROR: out-of-range output channel "
                          << e.device << " (max " << outputChannels - 1 << ") — "
                          << "this is a bug in routing construction." << std::endl;
                mEntries.clear();
                return;
            }
            mEntries.push_back(e);
            if (e.device > mMaxDeviceIndex) mMaxDeviceIndex = e.device;
        }
        mIdentity = checkIdentity(internalChannels, outputChannels);
        std::cout << "[OutputRouting] " << mEntries.size()
                  << " layout-derived routing entries"
                  << (mIdentity ? " — identity copy active"
                                : " — scatter routing active")
                  << std::endl;
    }

    // ── Accessors (read-only, safe to call from audio thread) ────────────

    bool identity() const { return mIdentity; }
    int  maxDeviceIndex() const { return mMaxDeviceIndex; }
    const std::vector<RemapEntry>& entries() const { return mEntries; }

    // ── Summary for logging ───────────────────────────────────────────────
    void print() const {
        if (mIdentity) {
            std::cout << "[OutputRemap] Identity mapping (no remapping applied)." << std::endl;
            return;
        }
        std::cout << "[OutputRemap] " << mEntries.size()
                  << " active entries, max device ch=" << mMaxDeviceIndex << ":" << std::endl;
        for (const auto& e : mEntries) {
            std::cout << "  layout " << e.layout << " → device " << e.device << std::endl;
        }
    }

private:

    // ── Data ──────────────────────────────────────────────────────────────
    std::vector<RemapEntry> mEntries;
    int  mMaxDeviceIndex = -1;
    bool mIdentity       = true;   // true by default (no CSV)

    // ── Helpers ──────────────────────────────────────────────────────────

    // Check if the current entries constitute a pure identity map.
    // Requires internalChannels == outputChannels — any gap in output channel
    // numbering means outputChannels > internalChannels, making scatter necessary.
    bool checkIdentity(int internalChannels, int outputChannels) const {
        if (outputChannels != internalChannels) return false;
        if (static_cast<int>(mEntries.size()) != internalChannels) return false;
        std::vector<bool> covered(internalChannels, false);
        for (const auto& e : mEntries) {
            if (e.layout != e.device) return false;
            if (covered[e.layout])   return false; // duplicate
            covered[e.layout] = true;
        }
        for (bool c : covered) {
            if (!c) return false;
        }
        return true;
    }

    // Split a CSV line by commas, trimming each field.
    static std::vector<std::string> splitCSV(const std::string& line) {
        std::vector<std::string> result;
        std::istringstream ss(line);
        std::string token;
        while (std::getline(ss, token, ',')) {
            trim(token);
            result.push_back(token);
        }
        return result;
    }

    // In-place trim whitespace from both ends.
    static void trim(std::string& s) {
        const char* ws = " \t\r\n";
        s.erase(0, s.find_first_not_of(ws));
        s.erase(s.find_last_not_of(ws) + 1);
    }

    // In-place ASCII lowercase.
    static void toLower(std::string& s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
};
