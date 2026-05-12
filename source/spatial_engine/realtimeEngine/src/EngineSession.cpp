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
#include "al/io/al_AudioIO.hpp"

#include <iostream>
#include <sstream>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// TeeStreamBuf — writes each character to two underlying stream buffers.
//
// Used to tee std::cout / std::cerr into an internal capture buffer while
// still forwarding output to the original terminal sink. This preserves all
// existing terminal output (requirement: do not hide terminal output) while
// allowing post-hoc failure diagnostics to be built from the captured text.
//
// Thread safety: not thread-safe. Only used during single-threaded startup
// stages (loadScene / applyLayout / start pre-loader). Never installed during
// audio callback execution.
// ─────────────────────────────────────────────────────────────────────────────
class TeeStreamBuf : public std::streambuf {
public:
    TeeStreamBuf(std::streambuf* primary, std::streambuf* secondary)
        : mPrimary(primary), mSecondary(secondary) {}
protected:
    int overflow(int c) override {
        if (c == traits_type::eof()) return traits_type::eof();
        if (mPrimary)   mPrimary->sputc(static_cast<char>(c));
        if (mSecondary) mSecondary->sputc(static_cast<char>(c));
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        if (mPrimary)   mPrimary->sputn(s, n);
        if (mSecondary) mSecondary->sputn(s, n);
        return n;
    }
    int sync() override {
        int primaryOk = 0;
        int secondaryOk = 0;
        if (mPrimary)   primaryOk = mPrimary->pubsync();
        if (mSecondary) secondaryOk = mSecondary->pubsync();
        return (primaryOk == 0 && secondaryOk == 0) ? 0 : -1;
    }
private:
    std::streambuf* mPrimary;
    std::streambuf* mSecondary;
};

// ─────────────────────────────────────────────────────────────────────────────
// StageCapture — RAII guard that tees std::cout and std::cerr into a buffer.
//
// Install at the top of a startup stage. On success, discard captured text
// (terminal already received everything). On failure, call captured() to get
// the text for the failure diagnostic block before this object is destroyed.
//
// restore() may be called explicitly before the destructor (e.g. to stop
// capture before starting background threads that also write to cout/cerr).
// ─────────────────────────────────────────────────────────────────────────────
class StageCapture {
public:
    StageCapture() {
        mOrigCout = std::cout.rdbuf();
        mOrigCerr = std::cerr.rdbuf();
        mTeeCout = std::make_unique<TeeStreamBuf>(mOrigCout, mCapture.rdbuf());
        mTeeCerr = std::make_unique<TeeStreamBuf>(mOrigCerr, mCapture.rdbuf());
        std::cout.rdbuf(mTeeCout.get());
        std::cerr.rdbuf(mTeeCerr.get());
    }
    ~StageCapture() { restore(); }

    // Restores cout/cerr to their original buffers. Safe to call multiple times.
    void restore() {
        if (mOrigCout) { std::cout.rdbuf(mOrigCout); mOrigCout = nullptr; }
        if (mOrigCerr) { std::cerr.rdbuf(mOrigCerr); mOrigCerr = nullptr; }
    }

    std::string captured() const { return mCapture.str(); }

private:
    std::streambuf*  mOrigCout = nullptr;
    std::streambuf*  mOrigCerr = nullptr;
    std::ostringstream mCapture;
    std::unique_ptr<TeeStreamBuf> mTeeCout;
    std::unique_ptr<TeeStreamBuf> mTeeCerr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Unit-conversion and range-clamping helpers (file-local, no external linkage).
// All parameter mutations in EngineSession flow through these to ensure
// consistent clamping regardless of call site.
// ─────────────────────────────────────────────────────────────────────────────

static float clampDb(float dB)
{
    if (!std::isfinite(dB)) return -60.0f;
    return std::max(-60.0f, std::min(12.0f, dB));
}

static float clampFocus(float f)
{
    if (!std::isfinite(f)) return 1.5f;
    return std::max(0.1f, std::min(5.0f, f));
}

static float dbToLinear(float dB)    { return std::pow(10.0f, dB / 20.0f); }

static float linearToDb(float lin)
{
    if (!std::isfinite(lin)) return (lin > 0.0f) ? 12.0f : -60.0f;
    return (lin <= 0.0f) ? -60.0f : 20.0f * std::log10(lin);
}

// ─────────────────────────────────────────────────────────────────────────────

struct EngineSession::OscParams {
    al::Parameter gainDb{"gain_db", "realtime", 0.0f, -60.0f, 12.0f};
    al::Parameter focus{"focus", "realtime", 1.5f, 0.1f, 5.0f};
    al::Parameter spkMixDb{"speaker_mix_db", "realtime", 0.0f, -60.0f, 12.0f};
    al::Parameter subMixDb{"sub_mix_db", "realtime", 0.0f, -60.0f, 12.0f};
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

std::string EngineSession::getFailureDiagnostics() const
{
    return mFailureDiagnostics;
}

void EngineSession::storeFailureDiagnostics(const std::string& stage,
                                            const std::string& capturedOutput)
{
    std::ostringstream oss;
    oss << "=== Failure diagnostics ===\n";
    oss << "Stage: " << stage << "\n";
    if (!mConfig.scenePath.empty())
        oss << "Scene: " << mConfig.scenePath << "\n";
    if (!mConfig.layoutPath.empty())
        oss << "Layout: " << mConfig.layoutPath << "\n";
    if (!mConfig.admFile.empty())
        oss << "ADM: " << mConfig.admFile << "\n";
    if (!mConfig.sourcesFolder.empty())
        oss << "Sources: " << mConfig.sourcesFolder << "\n";
    oss << "Error: " << mLastError << "\n";
    if (!capturedOutput.empty()) {
        oss << "Terminal output:\n" << capturedOutput;
        // Ensure the captured block ends with a newline before the footer.
        if (capturedOutput.back() != '\n') oss << "\n";
    }
    oss << "=== End failure diagnostics ===";
    mFailureDiagnostics = oss.str();
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
    mFailureDiagnostics.clear();
    mConfig.scenePath = sceneIn.scenePath;
    mConfig.sourcesFolder = sceneIn.sourcesFolder;
    mConfig.admFile = sceneIn.admFile;

    // Tee stdout+stderr into capture buffer for the duration of this stage.
    // No background threads are active here, so rdbuf redirect is safe.
    StageCapture cap;

    std::cout << "[EngineSession] Loading LUSID scene: " << mConfig.scenePath << std::endl;
    mSceneData = std::make_unique<SpatialData>();
    try {
        *mSceneData = JSONLoader::loadLusidScene(mConfig.scenePath);
    } catch (const std::exception& e) {
        setLastError(std::string("Failed to load LUSID scene: ") + e.what());
        cap.restore();
        storeFailureDiagnostics("load scene", cap.captured());
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
            cap.restore();
            storeFailureDiagnostics("load scene (ADM streaming)", cap.captured());
            return false;
        }
    } else {
        if (!mStreaming->loadScene(*mSceneData)) {
            setLastError("No source files could be loaded.");
            cap.restore();
            storeFailureDiagnostics("load scene (mono sources)", cap.captured());
            return false;
        }
    }

    std::cout << "[EngineSession] " << mStreaming->numSources() << " sources ready for streaming." << std::endl;
    return true;
}

bool EngineSession::applyLayout(const LayoutInput& layoutIn)
{
    mFailureDiagnostics.clear();

    if (!mSceneData) {
        setLastError("loadScene must be called successfully before applyLayout.");
        storeFailureDiagnostics("apply layout", "");
        return false;
    }

    mConfig.layoutPath = layoutIn.layoutPath;
    mRemapCsv = layoutIn.remapCsvPath;

    // Tee stdout+stderr for the duration of this stage (no background threads active).
    StageCapture cap;

    std::cout << "[EngineSession] Loading speaker layout: " << mConfig.layoutPath << std::endl;
    SpeakerLayoutData layout;
    try {
        layout = LayoutLoader::loadLayout(mConfig.layoutPath);
    } catch (const std::exception& e) {
        setLastError(std::string("Failed to load speaker layout: ") + e.what());
        cap.restore();
        storeFailureDiagnostics("apply layout", cap.captured());
        return false;
    }
    std::cout << "[EngineSession] Layout loaded: " << layout.speakers.size()
              << " speakers, " << layout.subwoofers.size() << " subwoofers." << std::endl;

    mPose = std::make_unique<Pose>(mConfig, mState);
    if (!mPose->loadScene(*mSceneData, layout)) {
        setLastError("Pose agent failed to initialize.");
        cap.restore();
        storeFailureDiagnostics("apply layout (Pose init)", cap.captured());
        return false;
    }
    std::cout << "[EngineSession] Pose agent ready: " << mPose->numSources()
              << " source positions will be computed per block." << std::endl;

    mSpatializer = std::make_unique<Spatializer>(mConfig, mState);
    if (!mSpatializer->init(layout)) {
        setLastError("Spatializer initialization failed.");
        cap.restore();
        storeFailureDiagnostics("apply layout (Spatializer init)", cap.captured());
        return false;
    }
    std::cout << "[EngineSession] Spatializer ready: DBAP with " << mSpatializer->numSpeakers()
              << " speakers, focus=" << mConfig.dbapFocus.load() << "." << std::endl;
    std::cout << "[EngineSession] Output channels (from layout): " << mConfig.outputChannels << std::endl;

    mSpatializer->prepareForSources(mPose->numSources());

    // Output routing is layout-derived — initialize it here while mSpatializer is fresh.
    configureOutputRouting();

    return true;
}

RuntimeParams EngineSession::sanitizeRuntimeParams(const RuntimeParams& params) const
{
    RuntimeParams clean;
    clean.masterGainDb = clampDb(params.masterGainDb);
    clean.dbapFocus    = clampFocus(params.dbapFocus);
    clean.speakerMixDb = clampDb(params.speakerMixDb);
    clean.subMixDb     = clampDb(params.subMixDb);
    return clean;
}

void EngineSession::applyRuntimeParamsToConfig(const RuntimeParams& params)
{
    mConfig.masterGain.store(dbToLinear(params.masterGainDb),    std::memory_order_relaxed);
    mConfig.dbapFocus.store(params.dbapFocus,                    std::memory_order_relaxed);
    mConfig.loudspeakerMix.store(dbToLinear(params.speakerMixDb), std::memory_order_relaxed);
    mConfig.subMix.store(dbToLinear(params.subMixDb),            std::memory_order_relaxed);
}

bool EngineSession::configureOutputRouting()
{
    // Output routing setup — layout-derived by default, with deprecated CSV override.
    // Called from applyLayout() after Spatializer::init(), so mSpatializer is valid here.
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

bool EngineSession::configureRuntime(const RuntimeParams& params)
{
    RuntimeParams clean = sanitizeRuntimeParams(params);
    applyRuntimeParamsToConfig(clean);

    // Sync OSC-visible parameter values if the server is already running
    // (i.e. configureRuntime called after start(), e.g. via resetRuntimeParams).
    // The change callbacks only write to the same atomics we just stored — no loop.
    if (mOscParams) {
        mOscParams->gainDb.set(clean.masterGainDb);
        mOscParams->focus.set(clean.dbapFocus);
        mOscParams->spkMixDb.set(clean.speakerMixDb);
        mOscParams->subMixDb.set(clean.subMixDb);
    }

    return true;
}

RuntimeParams EngineSession::getRuntimeParams() const
{
    RuntimeParams p;
    p.masterGainDb = linearToDb(mConfig.masterGain.load(std::memory_order_relaxed));
    p.dbapFocus    = mConfig.dbapFocus.load(std::memory_order_relaxed);
    p.speakerMixDb = linearToDb(mConfig.loudspeakerMix.load(std::memory_order_relaxed));
    p.subMixDb     = linearToDb(mConfig.subMix.load(std::memory_order_relaxed));
    return sanitizeRuntimeParams(p);
}

bool EngineSession::resetRuntimeParams()
{
    return configureRuntime(RuntimeParams::defaults());
}

bool EngineSession::start()
{
    mFailureDiagnostics.clear();

    // Tee stdout+stderr for the pre-loader phase of startup (single-threaded).
    // Capture is RESTORED before startLoader() to avoid racing with the loader
    // background thread. Failures after that point include context info only.
    StageCapture cap;

    // OSC server: only started if mOscPort > 0.
    // oscPort = 0 means "disable OSC" — no server is created.
    // Discovery Task B: al::ParameterServer on port 0 binds an OS-assigned
    // ephemeral port rather than acting as a no-op, so the guard is required
    // to honor the documented oscPort = 0 contract.
    if (mOscPort > 0) {
        mParamServer = std::make_unique<al::ParameterServer>("127.0.0.1", mOscPort);
        mOscParams = std::make_unique<OscParams>();

        // Initialize OSC params from the current staged runtime state so that any
        // configureRuntime() calls made before start() are visible to OSC clients.
        RuntimeParams staged = getRuntimeParams();
        mOscParams->gainDb.set(staged.masterGainDb);
        mOscParams->focus.set(staged.dbapFocus);
        mOscParams->spkMixDb.set(staged.speakerMixDb);
        mOscParams->subMixDb.set(staged.subMixDb);
        mOscParams->paused.set(mConfig.paused.load() ? 1.0f : 0.0f);
        mOscParams->elevMode.set(static_cast<float>(mConfig.elevationMode.load(std::memory_order_relaxed)));

        mOscParams->gainDb.registerChangeCallback([this](float dB) {
            this->mConfig.masterGain.store(std::pow(10.0f, dB / 20.0f), std::memory_order_relaxed);
        });

        mOscParams->focus.registerChangeCallback([this](float v) {
            this->mConfig.dbapFocus.store(clampFocus(v), std::memory_order_relaxed);
        });

        mOscParams->spkMixDb.registerChangeCallback([this](float dB) {
            this->mConfig.loudspeakerMix.store(std::pow(10.0f, dB / 20.0f), std::memory_order_relaxed);
        });

        mOscParams->subMixDb.registerChangeCallback([this](float dB) {
            this->mConfig.subMix.store(std::pow(10.0f, dB / 20.0f), std::memory_order_relaxed);
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

        *mParamServer << mOscParams->gainDb << mOscParams->focus << mOscParams->spkMixDb
                      << mOscParams->subMixDb << mOscParams->paused
                      << mOscParams->elevMode;

        if (!mParamServer->serverRunning()) {
            setLastError("ParameterServer failed to start.");
            cap.restore();
            storeFailureDiagnostics("start (OSC server)", cap.captured());
            return false;
        }
    }

    mBackend = std::make_unique<RealtimeBackend>(mConfig, mState);
    if (!mBackend->init()) {
        setLastError(mBackend->getLastError().empty()
                         ? std::string("Backend initialization failed.")
                         : mBackend->getLastError());
        cap.restore();
        storeFailureDiagnostics("start (audio backend init)", cap.captured());
        return false;
    }

    mBackend->setStreaming(mStreaming.get());
    mBackend->setPose(mPose.get());
    mBackend->setSpatializer(mSpatializer.get());
    mBackend->cacheSourceNames(mStreaming->sourceNames());

    // Restore capture BEFORE starting the loader background thread to avoid
    // any race between the loader's stdout/cerr writes and our rdbuf redirect.
    cap.restore();

    mStreaming->startLoader();

    if (!mBackend->start()) {
        setLastError(mBackend->getLastError().empty()
                         ? std::string("Backend failed to start.")
                         : mBackend->getLastError());
        // No captured output here (loader thread is running); include context only.
        storeFailureDiagnostics("start (audio stream)", "");
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

void EngineSession::setMasterGainDb(float dB)
{
    mConfig.masterGain.store(dbToLinear(clampDb(dB)), std::memory_order_relaxed);
}

void EngineSession::setDbapFocus(float focus)
{
    mConfig.dbapFocus.store(clampFocus(focus), std::memory_order_relaxed);
}

void EngineSession::setSpeakerMixDb(float dB)
{
    mConfig.loudspeakerMix.store(dbToLinear(clampDb(dB)), std::memory_order_relaxed);
}

void EngineSession::setSubMixDb(float dB)
{
    mConfig.subMix.store(dbToLinear(clampDb(dB)), std::memory_order_relaxed);
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
    st.requestedSampleRate = mConfig.sampleRate;
    st.outputDeviceName = mConfig.outputDeviceName.empty() ? "(system default)" : mConfig.outputDeviceName;
    if (mBackend) {
        st.audioBackendLabel = mBackend->backendDisplayLabel();
        st.outputDeviceName = mBackend->selectedDeviceName().empty()
                                ? st.outputDeviceName
                                : mBackend->selectedDeviceName();
        st.outputDevicePreferredSampleRate = mBackend->selectedDevicePreferredSampleRate();
        st.outputDevicePreferredSampleRateKnown = mBackend->selectedDevicePreferredSampleRateKnown();
        st.effectiveStreamSampleRate = mBackend->effectiveStreamSampleRate();
        st.effectiveStreamSampleRateKnown = mBackend->effectiveStreamSampleRateKnown();
    } else {
        const std::string backendFamily = al::AudioIO::compiledBackendName();
        if (backendFamily == "RtAudio") {
            // Pre-start: the active API is not confirmed until a stream is opened.
            // Avoid probing defaultBackendApiDisplayName() here — on Linux with JACK
            // compiled in, a temporary RtAudio() may select UNIX_JACK before the
            // server is known to be running. The post-start label comes from the
            // actual open stream via mBackend->backendDisplayLabel().
            st.audioBackendLabel = "RtAudio API unknown";
        } else if (!backendFamily.empty()) {
            const std::string backendApi = al::AudioIO::defaultBackendApiDisplayName();
            st.audioBackendLabel =
                (backendApi.empty() || backendApi == "Unknown") ? backendFamily
                                                                : backendFamily + " / " + backendApi;
        } else {
            st.audioBackendLabel = "Unknown backend";
        }
    }
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
