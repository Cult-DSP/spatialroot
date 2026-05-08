#pragma once

#include <string>
#include <map>
#include <vector>

struct Keyframe {
    double time;
    float x, y, z;
};

// Time unit for keyframe timestamps
// Used to convert all times to seconds during loading
enum class TimeUnit {
    Seconds,      // Default: times are already in seconds
    Samples,      // Times are sample indices (divide by sampleRate)
    Milliseconds  // Times are in milliseconds (divide by 1000)
};

struct SpatialData {
    int sampleRate;
    TimeUnit timeUnit = TimeUnit::Seconds;  // Explicit time unit from JSON
    std::map<std::string, std::vector<Keyframe>> sources;
    double duration = -1.0; // Duration in seconds from LUSID metadata, -1 if not specified
};

class JSONLoader {
public:
    /// Load a LUSID scene JSON file (v0.5+).
    /// Parses frames/nodes, extracts audio_object + direct_speaker as sources,
    /// LFE as "LFE" source. Ignores spectral_features and agent_state.
    /// Source keys use node ID format ("1.1", "11.1") not old "src_N" format.
    static SpatialData loadLusidScene(const std::string &path);

    /// DEPRECATED: Load old renderInstructions.json format.
    /// Kept for backwards compatibility. Use loadLusidScene() for new pipeline.
    /// Old implementation moved to old_schema_loader/JSONLoader.cpp
    static SpatialData loadSpatialInstructions(const std::string &path);
};
