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

struct EngineSession::OscParams {
    al::Parameter gain{"gain", "realtime", 0.5f, 0.1f, 3.0f};
    al::Parameter focus{"focus", "realtime", 1.5f, 0.1f, 5.0f};
    al::Parameter spkMixDb{"speaker_mix_db", "realtime", 0.0f, -10.0f, 10.0f};
    al::Parameter subMixDb{"sub_mix_db", "realtime", 0.0f, -10.0f, 10.0f};
    al::ParameterBool paused{"paused", "realtime", 0.0f};
    al::Parameter elevMode{"elevation_mode", "realtime", 0.0f, 0.0f, 2.0f};
};

EngineSession::EngineSession()
{
}

EngineSession::~EngineSession()
{
    shutdown();
}

void EngineSession::setLastError(const std::string& err)
{
    mLastError = err;
}

std::string EngineSession::getLastError() const
{
    return mLastError;
}

bool EngineSession::configureEngine(const EngineOptions& opts)
{
    mConfig.sampleRate = opts.sampleRate;
    mConfig.bufferSize = opts.bufferSize;
    mConfig.outputDeviceName = opts.outputDeviceName;
    mOscPort = opts.oscPort;
    mConfig.elevationMode.store(static_cast<int>(opts.elevationMode), std::memory_order_relaxed);
    
    return true;
}

bool EngineSession::loadScene(const SceneInput& sceneIn)
{
    mConfig.scenePath = sceneIn.scenePath;
    mConfig.sourcesFolder = sceneIn.sourcesFolder;
    mConfig.admFile = sceneIn.admFile;

    std::cout << "[EngineSession] Loading LUSID scene: " << mConfig.scenePath << std::endl;
    mSceneData = std::make_unique<SpatialData>();
    try {
        *mSceneData = JSONLoader::loadLusidScene(mConfig.scenePath);
    } catch (const std::exception& e) {
        setLastError(std::string("Failed to load LUSID scene: ") + e.what());
        return false;
    }
    
    std::cout << "[EngineSession] Scene loaded: " << mSceneData->sources.size() << " sources";
    if (mSceneData->duration > 0) {
        std::cout << ", duration: " << mSceneData->duration << "s";
    }
    std::cout << "." << std::endl;

    mStreaming = std::make_unique<Streaming>(mConfig, mState);
    bool useADM = !mConfig.admFile.empty();

    if (useADM) {
        if (!mStreaming->loadSceneFromADM(*mSceneData, mConfig.admFile)) {
            setLastError("No source channels could be loaded from ADM.");
            return false;
        }
    } else {
        if (!mStreaming->loadScene(*mSceneData)) {
            setLastError("No source files could be loaded.");
            return false;
        }
    }

    std::cout << "[EngineSession] " << mStreaming->numSources() << " sources ready for streaming." << std::endl;
    return true;
}

bool EngineSession::applyLayout(const LayoutInput& layoutIn)
{
    if (!mSceneData) {
        setLastError("loadScene must be called successfully before applyLayout.");
        return false;
    }

    mConfig.layoutPath = layoutIn.layoutPath;
    mRemapCsv = layoutIn.remapCsvPath;

    std::cout << "[EngineSession] Loading speaker layout: " << mConfig.layoutPath << std::endl;
    SpeakerLayoutData layout;
    try {
        layout = LayoutLoader::loadLayout(mConfig.layoutPath);
    } catch (const std::exception& e) {
        setLastError(std::string("Failed to load speaker layout: ") + e.what());
        return false;
    }
    std::cout << "[EngineSession] Layout loaded: " << layout.speakers.size()
              << " speakers, " << layout.subwoofers.size() << " subwoofers." << std::endl;

    mPose = std::make_unique<Pose>(mConfig, mState);
    if (!mPose->loadScene(*mSceneData, layout)) {
        setLastError("Pose agent failed to initialize.");
        return false;
    }
    std::cout << "[EngineSession] Pose agent ready: " << mPose->numSources()
              << " source positions will be computed per block." << std::endl;
              
    mSpatializer = std::make_unique<Spatializer>(mConfig, mState);
    if (!mSpatializer->init(layout)) {
        setLastError("Spatializer initialization failed.");
        return false;
    }
    std::cout << "[EngineSession] Spatializer ready: DBAP with " << mSpatializer->numSpeakers()
              << " speakers, focus=" << mConfig.dbapFocus.load() << "." << std::endl;
    std::cout << "[EngineSession] Output channels (from layout): " << mConfig.outputChannels << std::endl;

    mSpatializer->prepareForSources(mPose->numSources());

    return true;
}

bool EngineSession::configureRuntime(const RuntimeParams& params)
{
    mConfig.masterGain.store(params.masterGain, std::memory_order_relaxed);
    mConfig.dbapFocus.store(std::max(params.dbapFocus, 0.1f), std::memory_order_relaxed);
    mConfig.loudspeakerMix.store(powf(10.0f, params.speakerMixDb / 20.0f), std::memory_order_relaxed);
    mConfig.subMix.store(powf(10.0f, params.subMixDb / 20.0f), std::memory_order_relaxed);
    mOutputRemap = std::make_unique<OutputRemap>();
    if (!mRemapCsv.empty()) {
        // DEPRECATED: CSV remap is not a supported user workflow.
        // Layout-derived output routing (built during Spatializer::init()) is
        // now standard. This path remains temporarily as internal scaffolding
        // during validation and will be removed once layout routing is verified.
        std::cerr << "[EngineSession] WARNING: --remap CSV is deprecated. "
                  << "Physical output routing is now derived from the speaker layout JSON. "
                  << "CSV support will be removed after validation." << std::endl;
        bool remapOk = mOutputRemap->load(mRemapCsv,
                                          mSpatializer->numInternalChannels(),
                                          mConfig.outputChannels);
        if (remapOk) {
            mSpatializer->setRemap(mOutputRemap.get());
        } else {
            std::cout << "[EngineSession] CSV load failed — retaining layout-derived routing."
                      << std::endl;
        }
    } else {
        std::cout << "[EngineSession] Layout-derived output routing active ("
                  << mSpatializer->numInternalChannels() << " internal → "
                  << mConfig.outputChannels << " output channels)." << std::endl;
        // mRemap is already set to &mOutputRouting by Spatializer::init().
    }

    return true;
}

bool EngineSession::start()
{
    // OSC server: only started if mOscPort > 0.
    // oscPort = 0 means "disable OSC" — no server is created.
    // Discovery Task B: al::ParameterServer on port 0 binds an OS-assigned
    // ephemeral port rather than acting as a no-op, so the guard is required
    // to honor the documented oscPort = 0 contract.
    if (mOscPort > 0) {
        mParamServer = std::make_unique<al::ParameterServer>("127.0.0.1", mOscPort);
        mOscParams = std::make_unique<OscParams>();

        mOscParams->gain.set(mConfig.masterGain.load());
        mOscParams->focus.set(mConfig.dbapFocus.load());
        mOscParams->spkMixDb.set((float)(20.0f * std::log10(mConfig.loudspeakerMix.load())));
        mOscParams->subMixDb.set((float)(20.0f * std::log10(mConfig.subMix.load())));
        mOscParams->paused.set(mConfig.paused.load() ? 1.0f : 0.0f);
        mOscParams->elevMode.set(static_cast<float>(mConfig.elevationMode.load(std::memory_order_relaxed)));

        mOscParams->gain.registerChangeCallback([this](float v) {
            this->mConfig.masterGain.store(v, std::memory_order_relaxed);
        });

        mOscParams->focus.registerChangeCallback([this](float v) {
            this->mConfig.dbapFocus.store(std::max(v, 0.1f), std::memory_order_relaxed);
        });

        mOscParams->spkMixDb.registerChangeCallback([this](float dB) {
            this->mConfig.loudspeakerMix.store(powf(10.0f, dB / 20.0f), std::memory_order_relaxed);
        });

        mOscParams->subMixDb.registerChangeCallback([this](float dB) {
            this->mConfig.subMix.store(powf(10.0f, dB / 20.0f), std::memory_order_relaxed);
        });

        mOscParams->paused.registerChangeCallback([this](float v) {
            bool p = (v >= 0.5f);
            this->mConfig.paused.store(p, std::memory_order_relaxed);
        });

        mOscParams->elevMode.registerChangeCallback([this](float v) {
            int mode = static_cast<int>(std::round(v));
            mode = std::max(0, std::min(2, mode));
            this->mConfig.elevationMode.store(mode, std::memory_order_relaxed);
        });

        *mParamServer << mOscParams->gain << mOscParams->focus << mOscParams->spkMixDb
                      << mOscParams->subMixDb << mOscParams->paused
                      << mOscParams->elevMode;

        if (!mParamServer->serverRunning()) {
            setLastError("ParameterServer failed to start.");
            return false;
        }
    }

    mBackend = std::make_unique<RealtimeBackend>(mConfig, mState);
    if (!mBackend->init()) {
        setLastError("Backend initialization failed.");
        return false;
    }

    mBackend->setStreaming(mStreaming.get());
    mBackend->setPose(mPose.get());
    mBackend->setSpatializer(mSpatializer.get());
    mBackend->cacheSourceNames(mStreaming->sourceNames());

    mStreaming->startLoader();

    if (!mBackend->start()) {
        setLastError("Backend failed to start.");
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
    if (mOscParams) {
        mOscParams.reset();
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

void EngineSession::setPaused(bool isPaused)
{
    mConfig.paused.store(isPaused, std::memory_order_relaxed);
}

void EngineSession::setMasterGain(float gain)
{
    mConfig.masterGain.store(gain, std::memory_order_relaxed);
}

void EngineSession::setDbapFocus(float focus)
{
    mConfig.dbapFocus.store(std::max(focus, 0.1f), std::memory_order_relaxed);
}

void EngineSession::setSpeakerMixDb(float dB)
{
    mConfig.loudspeakerMix.store(powf(10.0f, dB / 20.0f), std::memory_order_relaxed);
}

void EngineSession::setSubMixDb(float dB)
{
    mConfig.subMix.store(powf(10.0f, dB / 20.0f), std::memory_order_relaxed);
}

void EngineSession::setElevationMode(ElevationMode mode)
{
    mConfig.elevationMode.store(static_cast<int>(mode), std::memory_order_relaxed);
}

void EngineSession::update()
{
    // No deferred work currently. Retained for API stability.
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
    st.isExitRequested = (mBackend && !mBackend->isRunning()); 
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
