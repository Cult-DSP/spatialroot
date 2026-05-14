#pragma once

#include "RealtimeTypes.hpp"
#include <memory>
#include <string>
#include <atomic>

// Forward declarations
class Streaming;
class Pose;
class Spatializer;
class RealtimeBackend;
class OutputRemap;
struct SpatialData; // Forward declare Scene data container
namespace al { class ParameterServer; }

struct EngineStatus {
    double timeSec;
    float cpuLoad;
    uint64_t renderActiveMask;
    uint64_t deviceActiveMask;
    uint64_t renderDomMask;
    uint64_t deviceDomMask;
    float mainRms;
    float subRms;
    size_t xruns;
    uint64_t nanGuardCount;
    uint64_t speakerProximityCount;
    bool paused;
    bool isExitRequested; // Added for main thread polling
    std::string audioBackendLabel;
    int requestedSampleRate = 48000;
    double effectiveStreamSampleRate = 0.0;
    bool effectiveStreamSampleRateKnown = false;
    int outputDeviceId = -1;
    std::string outputDeviceName;
    double outputDevicePreferredSampleRate = 0.0;
    bool outputDevicePreferredSampleRateKnown = false;
};

struct DiagnosticEvents {
    bool renderRelocEvent;
    uint64_t renderRelocPrev;
    uint64_t renderRelocNext;

    bool deviceRelocEvent;
    uint64_t deviceRelocPrev;
    uint64_t deviceRelocNext;

    bool renderDomRelocEvent;
    uint64_t renderDomRelocPrev;
    uint64_t renderDomRelocNext;

    bool deviceDomRelocEvent;
    uint64_t deviceDomRelocPrev;
    uint64_t deviceDomRelocNext;

    bool renderClusterEvent;
    uint64_t renderClusterPrev;
    uint64_t renderClusterNext;

    bool deviceClusterEvent;
    uint64_t deviceClusterPrev;
    uint64_t deviceClusterNext;
};

// --- New Core API Typed Structs (per Design Doc) ---
struct EngineOptions {
    int sampleRate = 48000;
    int bufferSize = 512;
    int outputDeviceId = -1;
    std::string outputDeviceName;
    int oscPort = 9009;
    ElevationMode elevationMode = ElevationMode::RescaleAtmosUp;
};

struct SceneInput {
    std::string scenePath;
    std::string sourcesFolder;
    std::string admFile;
};

struct LayoutInput {
    std::string layoutPath;
    // DEPRECATED: physical output routing is now derived from the layout JSON's
    // deviceChannel values via Spatializer::init(). This field is retained
    // temporarily as internal scaffolding only. Not a supported user workflow.
    // Will be removed after layout-routing validation is complete.
    std::string remapCsvPath;
};

struct RuntimeParams {
    float masterGainDb = 0.0f;   // Master gain in dB. Range: -60–+12 dB. 0 dB = unity.
    float dbapFocus    = 1.5f;   // DBAP rolloff exponent. Range: 0.1–5.0.
    float speakerMixDb = 0.0f;   // Post-DBAP main-channel trim in dB. Range: -60–+12 dB.
    float subMixDb     = 0.0f;   // Post-DBAP sub-channel trim in dB. Range: -60–+12 dB.

    // Canonical defaults — single source of truth for all callers (API, CLI, GUI).
    static RuntimeParams defaults() { return RuntimeParams(); }
};

class EngineSession {
public:
    EngineSession();
    ~EngineSession();

    bool configureEngine(const EngineOptions& opts);
    bool loadScene(const SceneInput& sceneIn);
    bool applyLayout(const LayoutInput& layoutIn);
    // Apply runtime DSP parameters. Safe before start() and after start().
    // Does not perform output routing setup (moved to applyLayout()).
    bool configureRuntime(const RuntimeParams& params);
    bool start();
    void shutdown();

    void setPaused(bool isPaused); // Transport control API

    // Returns current runtime params in user-facing units (dB for gains).
    // Reflects the latest values from setters, OSC, configureRuntime, or resetRuntimeParams.
    RuntimeParams getRuntimeParams() const;

    // Equivalent to configureRuntime(RuntimeParams::defaults()).
    // Safe before start() (updates staged params) and after start() (updates live).
    // Does not restart playback, reload scene/layout, or affect transport.
    bool resetRuntimeParams();

    // V1.1 runtime setter surface — safe to call after start(), before shutdown().
    // All writes use std::memory_order_relaxed, identical to the OSC callback implementations.
    // Calling before start() stages the value; it will be active when the engine starts.
    // Note: individual setters do not sync OSC param values. Use configureRuntime() or
    // resetRuntimeParams() when OSC sync is needed.
    void setMasterGainDb(float dB);
    void setDbapFocus(float focus);
    void setSpeakerMixDb(float dB);
    void setSubMixDb(float dB);
    void setElevationMode(ElevationMode mode);

    void update();

    EngineStatus queryStatus() const;
    DiagnosticEvents consumeDiagnostics();
    std::string getLastError() const;
    // Returns a structured failure diagnostic block captured during the most
    // recent failed stage (loadScene / applyLayout / start). Empty on success.
    // The block includes stage context, input paths, error message, and the
    // stdout+stderr output emitted during the failed operation. Content is
    // suitable for appending directly to the GUI engine log.
    std::string getFailureDiagnostics() const;

private:
    void setLastError(const std::string& err);
    // Builds and stores mFailureDiagnostics from captured stdout+stderr output.
    void storeFailureDiagnostics(const std::string& stage, const std::string& capturedOutput);

    // Clamp and sanitize params to valid ranges. Pure function — no side effects.
    RuntimeParams sanitizeRuntimeParams(const RuntimeParams& params) const;
    // Write sanitized params to mConfig atomics. Does not touch OSC or layout state.
    void applyRuntimeParamsToConfig(const RuntimeParams& params);
    // Initialize output routing from layout-derived mapping (or deprecated CSV override).
    // Must be called after Spatializer::init(). Called at the end of applyLayout().
    bool configureOutputRouting();

    RealtimeConfig mConfig;
    EngineState mState;
    std::string mLastError;
    std::string mFailureDiagnostics;

    std::unique_ptr<SpatialData> mSceneData; // Held securely between loadScene and applyLayout

    std::unique_ptr<Streaming> mStreaming;
    std::unique_ptr<Pose> mPose;
    std::unique_ptr<Spatializer> mSpatializer;
    std::unique_ptr<RealtimeBackend> mBackend;
    std::unique_ptr<OutputRemap> mOutputRemap;
    std::unique_ptr<al::ParameterServer> mParamServer;
    struct OscParams;
    std::unique_ptr<OscParams> mOscParams;

    int mOscPort = 9009;
    std::string mRemapCsv;
};
