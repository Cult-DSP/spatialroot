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

class EngineSession {
public:
    EngineSession();
    ~EngineSession();

    bool configureEngine();
    bool loadScene();
    bool applyLayout();
    bool configureRuntime(int oscPort, const std::string& remapCsv);
    bool start();
    void shutdown();

    void update();

    EngineStatus queryStatus() const;
    DiagnosticEvents consumeDiagnostics();

    RealtimeConfig& config() { return mConfig; }
    const RealtimeConfig& config() const { return mConfig; }

    EngineState& state() { return mState; }
    const EngineState& state() const { return mState; }

private:
    RealtimeConfig mConfig;
    EngineState mState;

    std::unique_ptr<Streaming> mStreaming;
    std::unique_ptr<Pose> mPose;
    std::unique_ptr<Spatializer> mSpatializer;
    std::unique_ptr<RealtimeBackend> mBackend;
    std::unique_ptr<OutputRemap> mOutputRemap;
    std::unique_ptr<al::ParameterServer> mParamServer;

    std::atomic<bool> mPendingAutoComp;
    int mOscPort = 9009;
    std::string mRemapCsv;
};
