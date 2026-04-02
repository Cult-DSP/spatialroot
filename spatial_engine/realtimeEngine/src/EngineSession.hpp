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
    std::string remapCsvPath;
};

struct RuntimeParams {
    float masterGain = 0.5f;
    float dbapFocus = 1.5f;
    float speakerMixDb = 0.0f;
    float subMixDb = 0.0f;
    bool autoCompensation = false;
};

class EngineSession {
public:
    EngineSession();
    ~EngineSession();

    bool configureEngine(const EngineOptions& opts);
    bool loadScene(const SceneInput& sceneIn);
    bool applyLayout(const LayoutInput& layoutIn);
    bool configureRuntime(const RuntimeParams& params);
    bool start();
    void shutdown();

    void setPaused(bool isPaused); // Transport control API

    // V1.1 runtime setter surface — safe to call after start(), before shutdown().
    // All writes use std::memory_order_relaxed, identical to the OSC callback implementations.
    // Calling before start() is harmless (writes the atomics) but has no effect on the engine.
    void setMasterGain(float gain);
    void setDbapFocus(float focus);
    void setSpeakerMixDb(float dB);
    void setSubMixDb(float dB);
    void setAutoCompensation(bool enable);
    void setElevationMode(ElevationMode mode);

    void update();

    EngineStatus queryStatus() const;
    DiagnosticEvents consumeDiagnostics();
    std::string getLastError() const; // Standardized error access

private:
    void setLastError(const std::string& err);

    RealtimeConfig mConfig;
    EngineState mState;
    std::string mLastError;

    std::unique_ptr<SpatialData> mSceneData; // Held securely between loadScene and applyLayout

    std::unique_ptr<Streaming> mStreaming;
    std::unique_ptr<Pose> mPose;
    std::unique_ptr<Spatializer> mSpatializer;
    std::unique_ptr<RealtimeBackend> mBackend;
    std::unique_ptr<OutputRemap> mOutputRemap;
    std::unique_ptr<al::ParameterServer> mParamServer;
    struct OscParams;
    std::unique_ptr<OscParams> mOscParams;

    std::atomic<bool> mPendingAutoComp;
    int mOscPort = 9009;
    std::string mRemapCsv;
};
