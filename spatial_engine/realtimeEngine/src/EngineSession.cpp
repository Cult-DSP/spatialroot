#include "EngineSession.hpp"
#include "Streaming.hpp"
#include "Pose.hpp"
#include "Spatializer.hpp"
#include "RealtimeBackend.hpp"
#include "OutputRemap.hpp"
#include "JSONLoader.hpp"
#include "LayoutLoader.hpp"

#include "al/ui/al_Parameter.hpp"
#include "al/ui/al_ParameterServer.hpp"

#include <iostream>
#include <cmath>

EngineSession::EngineSession()
    : mPendingAutoComp(false)
{
}

EngineSession::~EngineSession()
{
    shutdown();
}

bool EngineSession::configureEngine()
{
    // currently populated by caller directly via config() or in initConfig
    return true;
}

bool EngineSession::loadScene()
{
    std::cout << "[EngineSession] Loading LUSID scene: " << mConfig.scenePath << std::endl;
    SpatialData scene;
    try {
        scene = JSONLoader::loadLusidScene(mConfig.scenePath);
    } catch (const std::exception& e) {
        std::cerr << "[EngineSession] FATAL: Failed to load LUSID scene: " << e.what() << std::endl;
        return false;
    }
    
    std::cout << "[EngineSession] Scene loaded: " << scene.sources.size() << " sources";
    if (scene.duration > 0) {
        std::cout << ", duration: " << scene.duration << "s";
    }
    std::cout << "." << std::endl;

    mStreaming = std::make_unique<Streaming>(mConfig, mState);
    bool useADM = !mConfig.admFile.empty();

    if (useADM) {
        if (!mStreaming->loadSceneFromADM(scene, mConfig.admFile)) {
            std::cerr << "[EngineSession] FATAL: No source channels could be loaded from ADM." << std::endl;
            return false;
        }
    } else {
        if (!mStreaming->loadScene(scene)) {
            std::cerr << "[EngineSession] FATAL: No source files could be loaded." << std::endl;
            return false;
        }
    }

    std::cout << "[EngineSession] " << mStreaming->numSources() << " sources ready for streaming." << std::endl;

    // Load layot here so we can create pose agent
    std::cout << "[EngineSession] Loading speaker layout: " << mConfig.layoutPath << std::endl;
    SpeakerLayoutData layout;
    try {
        layout = LayoutLoader::loadLayout(mConfig.layoutPath);
    } catch (const std::exception& e) {
        std::cerr << "[EngineSession] FATAL: Failed to load speaker layout: " << e.what() << std::endl;
        return false;
    }
    std::cout << "[EngineSession] Layout loaded: " << layout.speakers.size()
              << " speakers, " << layout.subwoofers.size() << " subwoofers." << std::endl;

    mPose = std::make_unique<Pose>(mConfig, mState);
    if (!mPose->loadScene(scene, layout)) {
        std::cerr << "[EngineSession] FATAL: Pose agent failed to initialize." << std::endl;
        return false;
    }
    std::cout << "[EngineSession] Pose agent ready: " << mPose->numSources()
              << " source positions will be computed per block." << std::endl;
              
    mSpatializer = std::make_unique<Spatializer>(mConfig, mState);
    if (!mSpatializer->init(layout)) {
        std::cerr << "[EngineSession] FATAL: Spatializer initialization failed." << std::endl;
        return false;
    }
    std::cout << "[EngineSession] Spatializer ready: DBAP with " << mSpatializer->numSpeakers()
              << " speakers, focus=" << mConfig.dbapFocus.load() << "." << std::endl;
    std::cout << "[EngineSession] Output channels (from layout): " << mConfig.outputChannels << std::endl;

    mSpatializer->prepareForSources(mPose->numSources());

    return true;
}

bool EngineSession::applyLayout()
{
    // Now done in loadScene as per Pose dependency
    return true;
}

bool EngineSession::configureRuntime(int oscPort, const std::string& remapCsv)
{
    mOscPort = oscPort;
    mRemapCsv = remapCsv;

    if (mConfig.focusAutoCompensation.load()) {
        std::cout << "[EngineSession] Focus auto-compensation ON - computing initial autoCompValue..." << std::endl;
        float comp = mSpatializer->computeFocusCompensation();
        std::cout << "[EngineSession] Initial auto-compensation: " << comp
                  << " (" << (20.0f * std::log10(comp)) << " dB)" << std::endl;
    }

    mOutputRemap = std::make_unique<OutputRemap>();
    if (!mRemapCsv.empty()) {
        std::cout << "[EngineSession] Loading output remap CSV: " << mRemapCsv << std::endl;
        bool remapOk = mOutputRemap->load(mRemapCsv, mConfig.outputChannels, mConfig.outputChannels);
        if (!remapOk) {
            std::cout << "[EngineSession] Remap load failed or resulted in identity." << std::endl;
        }
        mSpatializer->setRemap(mOutputRemap.get());
    } else {
        std::cout << "[EngineSession] No --remap provided." << std::endl;
    }

    return true;
}

bool EngineSession::start()
{
    mParamServer = std::make_unique<al::ParameterServer>("127.0.0.1", mOscPort);

    // Register OSC parameters...
    // We must heap-allocate the parameters so they live for the duration,
    // actually, we can add them to EngineSession or just static/leak them, 
    // but AlloLib parameters need to be kept alive.
    // Let's create proper members in EngineSession or use static for simplicity in this extraction, 
    // wait, al::Parameter can just be dynamically allocated and managed.
    // Actually, to fully match, let's just make static parameters here or store them in EngineSession.
    // We'll dynamically allocate them.
    
    // Using a simpler approach: AlloLib ParameterServer keeps a pointer to them, so we need to store them.
    // Wait, the original code had them as local variables in main, but they were kept alive because main() was blocking.
    // Let's add them to a small struct inside EngineSession.cpp, or just keep them as static.
    // Since we only run one session, static is acceptable for parameters.
    
    static al::Parameter gainParam{"gain", "realtime", mConfig.masterGain.load(), 0.1f, 3.0f};
    static al::Parameter focusParam{"focus", "realtime", mConfig.dbapFocus.load(), 0.2f, 5.0f};
    static al::Parameter spkMixDbParam{"speaker_mix_db", "realtime", (float)(20.0f * std::log10(mConfig.loudspeakerMix.load())), -10.0f, 10.0f};
    static al::Parameter subMixDbParam{"sub_mix_db", "realtime", (float)(20.0f * std::log10(mConfig.subMix.load())), -10.0f, 10.0f};
    static al::ParameterBool autoCompParam{"auto_comp", "realtime", mConfig.focusAutoCompensation.load() ? 1.0f : 0.0f};
    static al::ParameterBool pausedParam{"paused", "realtime", 0.0f};
    static al::Parameter elevModeParam{"elevation_mode", "realtime", static_cast<float>(mConfig.elevationMode.load(std::memory_order_relaxed)), 0.0f, 2.0f};

    gainParam.registerChangeCallback([this](float v) {
        this->mConfig.masterGain.store(v, std::memory_order_relaxed);
    });

    focusParam.registerChangeCallback([this](float v) {
        this->mConfig.dbapFocus.store(v, std::memory_order_relaxed);
        if (this->mConfig.focusAutoCompensation.load(std::memory_order_relaxed)) {
            this->mPendingAutoComp.store(true, std::memory_order_relaxed);
        }
    });

    spkMixDbParam.registerChangeCallback([this](float dB) {
        this->mConfig.loudspeakerMix.store(powf(10.0f, dB / 20.0f), std::memory_order_relaxed);
    });

    subMixDbParam.registerChangeCallback([this](float dB) {
        this->mConfig.subMix.store(powf(10.0f, dB / 20.0f), std::memory_order_relaxed);
    });

    autoCompParam.registerChangeCallback([this](float v) {
        bool enable = (v >= 0.5f);
        this->mConfig.focusAutoCompensation.store(enable, std::memory_order_relaxed);
        if (enable) this->mPendingAutoComp.store(true, std::memory_order_relaxed);
    });

    pausedParam.registerChangeCallback([this](float v) {
        bool p = (v >= 0.5f);
        this->mConfig.paused.store(p, std::memory_order_relaxed);
    });

    elevModeParam.registerChangeCallback([this](float v) {
        int mode = static_cast<int>(std::round(v));
        mode = std::max(0, std::min(2, mode));
        this->mConfig.elevationMode.store(mode, std::memory_order_relaxed);
    });

    *mParamServer << gainParam << focusParam << spkMixDbParam << subMixDbParam << autoCompParam << pausedParam << elevModeParam;

    if (!mParamServer->serverRunning()) {
        std::cerr << "[EngineSession] FATAL: ParameterServer failed to start." << std::endl;
        return false;
    }

    mBackend = std::make_unique<RealtimeBackend>(mConfig, mState);
    if (!mBackend->init()) {
        std::cerr << "[EngineSession] FATAL: Backend initialization failed." << std::endl;
        return false;
    }

    mBackend->setStreaming(mStreaming.get());
    mBackend->setPose(mPose.get());
    mBackend->setSpatializer(mSpatializer.get());
    mBackend->cacheSourceNames(mStreaming->sourceNames());

    mStreaming->startLoader();

    if (!mBackend->start()) {
        std::cerr << "[EngineSession] FATAL: Backend failed to start." << std::endl;
        mStreaming->shutdown();
        return false;
    }

    return true;
}

void EngineSession::shutdown()
{
    if (mParamServer) {
        mParamServer->stopServer();
        mParamServer.reset();
    }
    if (mBackend) {
        mBackend->shutdown();
        mBackend.reset();
    }
    if (mStreaming) {
        mStreaming->shutdown();
        mStreaming.reset();
    }
}

void EngineSession::update()
{
    if (mPendingAutoComp.load(std::memory_order_relaxed)) {
        mPendingAutoComp.store(false, std::memory_order_relaxed);
        if (mSpatializer) {
            float comp = mSpatializer->computeFocusCompensation();
            std::cout << "\n[EngineSession] Focus compensation recomputed: " << comp << std::endl;
        }
    }
}

EngineStatus EngineSession::queryStatus() const
{
    EngineStatus st;
    st.timeSec = mState.playbackTimeSec.load(std::memory_order_relaxed);
    st.cpuLoad = mState.callbackCpuLoad.load(std::memory_order_relaxed);
    st.renderActiveMask = mState.renderActiveMask.load(std::memory_order_relaxed);
    st.deviceActiveMask = mState.deviceActiveMask.load(std::memory_order_relaxed);
    st.renderDomMask = mState.renderDomMask.load(std::memory_order_relaxed);
    st.deviceDomMask = mState.deviceDomMask.load(std::memory_order_relaxed);
    st.mainRms = mState.mainRmsTotal.load(std::memory_order_relaxed);
    st.subRms = mState.subRmsTotal.load(std::memory_order_relaxed);
    st.xruns = mStreaming ? mStreaming->totalUnderruns() : 0;
    st.nanGuardCount = mState.nanGuardCount.load(std::memory_order_relaxed);
    st.speakerProximityCount = mState.speakerProximityCount.load(std::memory_order_relaxed);
    st.paused = mConfig.paused.load(std::memory_order_relaxed);
    return st;
}

DiagnosticEvents EngineSession::consumeDiagnostics()
{
    DiagnosticEvents ev;
    
    ev.renderRelocEvent = mState.renderRelocEvent.exchange(false, std::memory_order_relaxed);
    ev.renderRelocPrev = mState.renderRelocPrev.load(std::memory_order_relaxed);
    ev.renderRelocNext = mState.renderRelocNext.load(std::memory_order_relaxed);

    ev.deviceRelocEvent = mState.deviceRelocEvent.exchange(false, std::memory_order_relaxed);
    ev.deviceRelocPrev = mState.deviceRelocPrev.load(std::memory_order_relaxed);
    ev.deviceRelocNext = mState.deviceRelocNext.load(std::memory_order_relaxed);

    ev.renderDomRelocEvent = mState.renderDomRelocEvent.exchange(false, std::memory_order_relaxed);
    ev.renderDomRelocPrev = mState.renderDomRelocPrev.load(std::memory_order_relaxed);
    ev.renderDomRelocNext = mState.renderDomRelocNext.load(std::memory_order_relaxed);

    ev.deviceDomRelocEvent = mState.deviceDomRelocEvent.exchange(false, std::memory_order_relaxed);
    ev.deviceDomRelocPrev = mState.deviceDomRelocPrev.load(std::memory_order_relaxed);
    ev.deviceDomRelocNext = mState.deviceDomRelocNext.load(std::memory_order_relaxed);

    ev.renderClusterEvent = mState.renderClusterEvent.exchange(false, std::memory_order_relaxed);
    ev.renderClusterPrev = mState.renderClusterPrev.load(std::memory_order_relaxed);
    ev.renderClusterNext = mState.renderClusterNext.load(std::memory_order_relaxed);

    ev.deviceClusterEvent = mState.deviceClusterEvent.exchange(false, std::memory_order_relaxed);
    ev.deviceClusterPrev = mState.deviceClusterPrev.load(std::memory_order_relaxed);
    ev.deviceClusterNext = mState.deviceClusterNext.load(std::memory_order_relaxed);

    return ev;
}
