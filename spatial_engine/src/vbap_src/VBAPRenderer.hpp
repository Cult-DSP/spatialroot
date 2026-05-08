// VBAPRenderer - spatial audio renderer using AlloLib's VBAP implementation
//
// important notes if you need to debug this again:
// 
// 1. al::Speaker constructor expects angles in DEGREES not radians
//    the layout JSON has radians so we convert in the constructor
//    without this VBAP silently produces zeros
//
// 2. AlloSphere hardware uses non-consecutive channel numbers 1-60 with gaps
//    but we use consecutive 0-53 indices for VBAP and the output WAV
//    this avoids out-of-bounds crashes when accessing AudioIOData buffers
//    can remap to hardware channels later if needed
//
// 3. AudioIOData initialization order matters
//    must call framesPerBuffer before channelsOut or you get assertion failures
//
// 4. VBAP uses += to accumulate sources so call zeroOut before each block
//
// 5. must call audioIO.frame(0) before reading output samples
//
// 6. interpolateDir() must handle edge cases: empty keyframes, t outside range,
//    degenerate directions - see implementation for NaN-safe version

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <al/math/al_Vec.hpp>
#include <al/sound/al_Vbap.hpp>
#include <al/io/al_AudioIOData.hpp>

#include "../JSONLoader.hpp"
#include "../LayoutLoader.hpp"
#include "../WavUtils.hpp"

// Elevation handling mode for directions outside speaker layout coverage
enum class ElevationMode {
    Clamp,    // Hard clip elevation to layout bounds (may cause "sticking" at extremes)
    RescaleAtmosUp,  // (default) assumes content elevation in [0, +pi/2] and remaps into layout range
    RescaleFullSphere // assumes content elevation in [-pi/2, +pi/2] and remaps into layout range
};

// Render configuration options
struct RenderConfig {
    float masterGainDb = 0.0f;    // Master gain in dB. Range: -60–+12 dB. 0 dB = unity.
    float masterGainLinear() const { return std::pow(10.0f, masterGainDb / 20.0f); }
    std::string soloSource = "";    // If non-empty, only render this source
    double t0 = -1.0;               // Start time in seconds (-1 = from beginning)
    double t1 = -1.0;               // End time in seconds (-1 = to end)
    bool debugDiagnostics = true;  // Enable per-block diagnostics logging
    std::string debugOutputDir = "processedData/debug";  // Where to write debug files

    // Render resolution (controls direction update frequency):
    // - "block": direction computed once per block center (RECOMMENDED)
    //            Use small blockSize (32-64) for smooth motion, larger (256-512) for faster renders
    // - "sample": direction computed every sample (slowest, smoothest - use for debugging only)
    // 
    // NOTE: "smooth" mode is DEPRECATED - it interpolates VBAP gains linearly which can cause
    //       artifacts when the VBAP triangle changes within a block. Use "block" with small
    //       blockSize instead.
    
    std::string renderResolution = "block";
    int blockSize = 64;  // Recommended: 64 for quality, 256 for speed
    
    // Elevation mode for directions outside speaker layout coverage
    // Default: RescaleAtmosUp (vertical compensation ON)
    ElevationMode elevationMode = ElevationMode::RescaleAtmosUp;
    
    // Force 2D mode (flatten all elevations to z=0) - useful for testing
    bool force2D = false;
};

// Render statistics for diagnostics
struct RenderStats {
    std::vector<float> channelRMS;      // RMS level per channel in dBFS
    std::vector<float> channelPeak;     // Peak absolute value per channel
    std::vector<int> channelNaNCount;   // NaN count per channel
    std::vector<int> channelInfCount;   // Inf count per channel
    int totalSamples = 0;
    int numChannels = 0;
    int numSources = 0;
    double durationSec = 0.0;
    
    // Per-source fallback statistics
    std::unordered_map<std::string, int> sourceFallbackCount;
    int totalFallbackBlocks = 0;
};

class VBAPRenderer {
public:
    VBAPRenderer(const SpeakerLayoutData &layout,
                 const SpatialData &spatial,
                 const std::map<std::string, MonoWavData> &sources);

    // Main render with default config
    MultiWavData render();
    
    // Render with custom configuration
    MultiWavData render(const RenderConfig &config);
    
    // Get statistics from last render (call after render())
    RenderStats getLastRenderStats() const { return mLastStats; }

private:
    SpeakerLayoutData mLayout;
    SpatialData mSpatial;
    const std::map<std::string, MonoWavData> &mSources;
    
    al::Speakers mSpeakers;
    al::Vbap mVBAP;
    
    // not currently used but left here in case you need to remap channels later
    // would map consecutive VBAP indices to AlloSphere hardware channels. currently done in https://github.com/lucianpar/54ChanPlayer on allosphere playback end
    std::vector<int> mVbapToDevice;

   
    
    // Statistics from last render
    RenderStats mLastStats;
    
    // Layout-derived elevation constraints (computed from speaker positions)
    float mLayoutMinElRad = -1.5707963f;   // min elevation in radians (default: -pi/2)
    float mLayoutMaxElRad =  1.5707963f;   // max elevation in radians (default: +pi/2)
    float mLayoutElSpanRad = 3.1415926f;   // elevation span (maxEl - minEl)
    bool  mLayoutIs2D = false;             // true if layout has negligible elevation spread
    
    // Direction sanitization diagnostics
    struct DirDiag {
        uint64_t clampedEl = 0;      // directions where elevation was clamped
        uint64_t rescaledAtmosUp = 0;    // directions remapped by RescaleAtmosUp
        uint64_t rescaledFullSphere = 0; // directions remapped by RescaleFullSphere
        uint64_t flattened2D = 0;    // directions flattened to plane (2D mode)
        uint64_t invalidDir = 0;     // degenerate directions that needed fallback
    } mDirDiag;
    
    // VBAP rendering diagnostics (per-render, reset at start)
    struct VBAPDiag {
        std::unordered_map<std::string, uint64_t> zeroBlocks;      // blocks where VBAP produced ~silence despite input
        std::unordered_map<std::string, uint64_t> retargetBlocks;  // blocks retargeted to nearest speaker
        std::unordered_map<std::string, uint64_t> substeppedBlocks; // blocks that needed sub-stepping (fast motion)
        uint64_t totalZeroBlocks = 0;
        uint64_t totalRetargets = 0;
        uint64_t totalSubsteps = 0;
    } mVBAPDiag;
    
    // Precomputed speaker unit directions (for nearest-speaker fallback)
    std::vector<al::Vec3f> mSpeakerDirs;
    
    // Compile-time constants for VBAP robustness (developer tunable)
    // FUTURE OPTIMIZATION: These could become RenderConfig parameters
    static constexpr float kInputEnergyThreshold = 1e-4f;   // per-sample threshold for "has energy"
    static constexpr float kVBAPZeroThreshold = 1e-6f;      // output sum threshold for "VBAP failed"
    static constexpr float kFastMoverAngleRad = 0.25f;      // ~14 degrees - triggers sub-stepping
    static constexpr int kSubStepHop = 16;                  // sub-step size for fast movers
    
    // Per-source direction tracking for safe fallback
    // These are reset at start of each render
    std::unordered_map<std::string, al::Vec3f> mLastGoodDir;
    std::unordered_set<std::string> mWarnedDegenerate;
    std::unordered_map<std::string, int> mFallbackCount;

    // Helper: check if all components are finite
    static bool finite3(const al::Vec3f& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    
    // Helper: compute unit direction from Cartesian (safe, validates output)
    static al::Vec3f safeNormalize(const al::Vec3f& v);
    
    // Sanitize direction to fit within speaker layout's representable range
    // - 2D layouts: flatten elevation to z=0
    // - 3D layouts: clamp or rescale (RescaleAtmosUp / RescaleFullSphere) elevation to [mLayoutMinElRad, mLayoutMaxElRad]
    // This prevents sources from becoming inaudible due to out-of-range directions
    al::Vec3f sanitizeDirForLayout(const al::Vec3f& unitDir, ElevationMode mode);
    
    // Get safe direction for a source, using last-good or fallback if invalid
    // This is the main entry point for direction computation in the render loop
    al::Vec3f safeDirForSource(const std::string& name, const std::vector<Keyframe>& kfs, double t);

    // linear interpolation between spatial keyframes (raw, may return invalid)
    al::Vec3f interpolateDirRaw(const std::vector<Keyframe> &kfs, double t);
    
    // Compute statistics on rendered output
    void computeRenderStats(const MultiWavData &output);
    
    // Detect and fix keyframe time units (samples vs seconds)
    void normalizeKeyframeTimes(double durationSec, size_t totalSamples, int sr);
    
    // Reset per-render state (call at start of render)
    void resetPerRenderState();
    
    // Print end-of-render fallback summary
    void printFallbackSummary(int totalBlocks);
    
    // Print direction sanitization summary
    void printSanitizationSummary();
    
    // Print VBAP diagnostics summary (zero blocks, retargets, substeps)
    void printVBAPDiagSummary();
    
    // Find nearest speaker direction for fallback when VBAP fails
    // Returns direction slightly inside hull (90% toward speaker) for proper triangulation
    al::Vec3f nearestSpeakerDir(const al::Vec3f& dir);

    // Render implementations
    void renderPerBlock(MultiWavData &out, const RenderConfig &config,
                        size_t startSample, size_t endSample);
    
    void renderSmooth(MultiWavData &out, const RenderConfig &config,
                      size_t startSample, size_t endSample);
    
    void renderPerSample(MultiWavData &out, const RenderConfig &config,
                         size_t startSample, size_t endSample);
    
    // Compute VBAP gains for a direction
    void computeVBAPGains(const al::Vec3f& dir, std::vector<float>& gains);
    
    // Spherical linear interpolation between two directions
    // t=0 returns 'a', t=1 returns 'b', intermediate values smoothly interpolate on sphere
    static al::Vec3f slerpDir(const al::Vec3f& a, const al::Vec3f& b, float t);
};

