#include "JSONLoader.hpp"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Helper to check if a value is finite
static bool isFiniteValue(double v) {
    return std::isfinite(v);
}

// Helper to check if a keyframe has valid numeric fields
static bool isValidKeyframe(const Keyframe& kf) {
    return isFiniteValue(kf.time) && 
           isFiniteValue(kf.x) && 
           isFiniteValue(kf.y) && 
           isFiniteValue(kf.z);
}

// Parse timeUnit string to enum + multiplier
static std::pair<TimeUnit, double> parseTimeUnit(const std::string& timeUnitStr, int sampleRate) {
    TimeUnit unit = TimeUnit::Seconds;
    double multiplier = 1.0;

    if (timeUnitStr == "seconds" || timeUnitStr == "s") {
        unit = TimeUnit::Seconds;
        multiplier = 1.0;
        std::cout << "Time unit: seconds (no conversion)\n";
    } else if (timeUnitStr == "samples" || timeUnitStr == "samp") {
        unit = TimeUnit::Samples;
        multiplier = 1.0 / (double)sampleRate;
        std::cout << "Time unit: samples (converting to seconds with sr=" << sampleRate << ")\n";
    } else if (timeUnitStr == "milliseconds" || timeUnitStr == "ms") {
        unit = TimeUnit::Milliseconds;
        multiplier = 0.001;
        std::cout << "Time unit: milliseconds (converting to seconds)\n";
    } else {
        std::cerr << "Warning: unknown timeUnit '" << timeUnitStr << "', assuming seconds\n";
    }

    return {unit, multiplier};
}

// ============================================================================
// NEW: Load LUSID scene format (v0.5+)
// ============================================================================

SpatialData JSONLoader::loadLusidScene(const std::string &path) {
    std::ifstream f(path);
    if (!f.good()) throw std::runtime_error("Cannot open LUSID scene JSON: " + path);

    json j;
    f >> j;

    SpatialData d;

    // Parse top-level fields
    d.sampleRate = j.value("sampleRate", 48000);

    std::string timeUnitStr = j.value("timeUnit", "seconds");
    auto [timeUnit, timeMultiplier] = parseTimeUnit(timeUnitStr, d.sampleRate);
    d.timeUnit = timeUnit;

    // Parse duration field if present (LUSID v0.5.2+)
    if (j.contains("duration") && j["duration"].is_number()) {
        d.duration = j["duration"].get<double>();
        std::cout << "LUSID scene duration: " << d.duration << " seconds\n";
    } else {
        d.duration = -1.0; // Not specified, will fall back to WAV file length
    }

    std::string version = j.value("version", "0.5");
    std::cout << "Loading LUSID scene v" << version << "\n";

    if (!j.contains("frames") || !j["frames"].is_array()) {
        std::cerr << "Warning: LUSID scene has no 'frames' array\n";
        return d;
    }

    int totalSources = 0;
    int totalDropped = 0;

    // Iterate over frames
    for (auto &frame : j["frames"]) {
        if (!frame.contains("time") || !frame["time"].is_number()) {
            std::cerr << "Warning: frame missing 'time', skipping\n";
            continue;
        }
        double frameTime = frame["time"].get<double>() * timeMultiplier;

        if (!frame.contains("nodes") || !frame["nodes"].is_array()) {
            continue;
        }

        for (auto &node : frame["nodes"]) {
            if (!node.contains("id") || !node.contains("type")) {
                continue;
            }

            std::string nodeId = node["id"].get<std::string>();
            std::string nodeType = node["type"].get<std::string>();

            // audio_object and direct_speaker → spatial sources
            if (nodeType == "audio_object" || nodeType == "direct_speaker") {
                if (!node.contains("cart") || !node["cart"].is_array() || node["cart"].size() < 3) {
                    totalDropped++;
                    continue;
                }

                Keyframe kf;
                kf.time = frameTime;
                kf.x = node["cart"][0].get<float>();
                kf.y = node["cart"][1].get<float>();
                kf.z = node["cart"][2].get<float>();

                if (!isValidKeyframe(kf)) {
                    totalDropped++;
                    continue;
                }

                // Check for zero-length direction vector
                float mag = std::sqrt(kf.x*kf.x + kf.y*kf.y + kf.z*kf.z);
                if (mag < 1e-8f) {
                    std::cerr << "Warning: node '" << nodeId << "' at t=" << kf.time 
                              << " has zero direction, setting to front (0,1,0)\n";
                    kf.x = 0.0f;
                    kf.y = 1.0f;
                    kf.z = 0.0f;
                }

                // Use node ID as source key (e.g., "1.1", "11.1")
                d.sources[nodeId].push_back(kf);
            }
            // LFE → special source with no cart
            else if (nodeType == "LFE") {
                // Only add LFE once (first occurrence)
                if (d.sources.find("LFE") == d.sources.end()) {
                    Keyframe kf;
                    kf.time = 0.0;
                    kf.x = 0.0f;
                    kf.y = 0.0f;
                    kf.z = 0.0f;
                    d.sources["LFE"].push_back(kf);
                }
            }
            // spectral_features, agent_state → ignored by renderer
        }
    }

    // Post-process: sort and deduplicate keyframes per source
    for (auto &[name, frames] : d.sources) {
        if (name == "LFE") continue;  // LFE only has one keyframe

        totalSources++;

        // Sort by time
        std::sort(frames.begin(), frames.end(),
                  [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });

        // Remove duplicate times (keep last occurrence within epsilon)
        const double timeEpsilon = 1e-6;
        std::vector<Keyframe> deduped;
        for (size_t i = 0; i < frames.size(); i++) {
            if (i + 1 < frames.size() && 
                std::abs(frames[i+1].time - frames[i].time) < timeEpsilon) {
                continue;  // Skip, keep later one
            }
            deduped.push_back(frames[i]);
        }

        if (deduped.size() < frames.size()) {
            std::cerr << "Warning: source '" << name << "' had " 
                      << (frames.size() - deduped.size()) 
                      << " duplicate-time keyframes collapsed\n";
        }

        frames = deduped;
    }

    if (totalDropped > 0) {
        std::cerr << "Total invalid keyframes dropped: " << totalDropped << "\n";
    }

    bool hasLFE = d.sources.find("LFE") != d.sources.end();
    std::cout << "Loaded LUSID scene: " << totalSources << " spatial sources"
              << (hasLFE ? " + LFE" : "") << "\n";

    return d;
}

// ============================================================================
// DEPRECATED: Load old renderInstructions.json format
// Kept for backwards compatibility. Implementation in old_schema_loader/.
// This is a thin wrapper that delegates to the old format parser.
// ============================================================================

SpatialData JSONLoader::loadSpatialInstructions(const std::string &path) {
    std::cerr << "WARNING: loadSpatialInstructions() is deprecated. Use loadLusidScene() instead.\n";

    // Parse old format inline (same logic as old_schema_loader/JSONLoader.cpp)
    std::ifstream f(path);
    if (!f.good()) throw std::runtime_error("Cannot open spatial JSON");

    json j;
    f >> j;

    SpatialData d;
    d.sampleRate = j["sampleRate"];
    
    std::string timeUnitStr = j.value("timeUnit", "seconds");
    auto [timeUnit, timeMultiplier] = parseTimeUnit(timeUnitStr, d.sampleRate);
    d.timeUnit = timeUnit;

    for (auto &[name, kflist] : j["sources"].items()) {
        std::vector<Keyframe> frames;

        for (auto &k : kflist) {
            Keyframe kf;
            if (!k.contains("time") || !k["time"].is_number()) continue;
            kf.time = k["time"].get<double>() * timeMultiplier;
            
            if (!k.contains("cart") || !k["cart"].is_array() || k["cart"].size() < 3) continue;
            kf.x = k["cart"][0];
            kf.y = k["cart"][1];
            kf.z = k["cart"][2];
            
            if (!isValidKeyframe(kf)) continue;

            float mag = std::sqrt(kf.x*kf.x + kf.y*kf.y + kf.z*kf.z);
            if (mag < 1e-8f) {
                kf.x = 0.0f; kf.y = 1.0f; kf.z = 0.0f;
            }
            
            frames.push_back(kf);
        }
        
        std::sort(frames.begin(), frames.end(), 
                  [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
        
        d.sources[name] = frames;
    }

    return d;
}
