#pragma once
// App — Spatial Root ImGui + GLFW desktop GUI application.
//
// Owns the EngineSession, manages the engine state machine, and renders the
// Dear ImGui UI each frame via tick(). Two tabs: ENGINE and TRANSCODE.
//
// Architecture notes:
// - tick() is called once per render loop iteration from main.cpp.
// - The engine update cycle (update() / queryStatus() / consumeDiagnostics())
//   runs inside tick() at GLFW's event timeout rate (~20 Hz minimum).
// - Runtime setters are called immediately when ImGui widgets report a change,
//   following the immediate-mode paradigm — no debounce needed since ImGui
//   only returns true on actual value changes.
// - OSC is enabled by default (oscPort=9009) matching the Python GUI.
//   DEV NOTE: Evaluate whether to add an OSC enable/disable toggle in a future
//   iteration. For V1, always-on is simplest and matches current behaviour.

#include "DefaultLayoutManager.hpp"
#include "EngineSession.hpp"
#include "SpatialRootPaths.hpp"
#include "SubprocessRunner.hpp"
#include "imgui.h"

#include <atomic>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// ── Engine / app state machine ────────────────────────────────────────────────
enum class AppState {
    Idle,         // no session running
    Transcoding,  // cult-transcoder subprocess running (ADM WAV input)
    Running,      // engine running
    Paused,       // engine running but paused
    Error         // last operation failed
};

// ── Log entry (colour-coded) ──────────────────────────────────────────────────
struct LogEntry {
    ImVec4      color;
    std::string text;
};

class App {
public:
    // projectRoot: path to the spatialroot project root directory.
    // Used to resolve speaker layouts, build output paths, and cult-transcoder.
    // Defaults to "." (current working directory — works when run from project root).
    explicit App(std::string projectRoot = ".",
                 bool keepTempSessions = false,
                 std::string tempRootOverride = "");
    ~App();

    // Called once per render frame by main.cpp.
    // Advances the engine state machine and renders the full ImGui UI.
    void tick();

    // Call from the GLFW window close callback.
    // Shuts down the engine session cleanly before the window closes.
    void requestShutdown();

    bool shutdownRequested() const { return mShutdownRequested; }

private:
    // ── Project root & paths ─────────────────────────────────────────────
    std::string mProjectRoot;
    bool        mKeepTempSessions = false;
    std::string mTempRootOverride;

    // ── Default layout manager (persistent settings, not session temp) ───
    DefaultLayoutManager      mDefaultLayoutMgr;
    DefaultLayoutStatus       mDefaultLayoutStatus = DefaultLayoutStatus::None;
    std::string               mDefaultLayoutSavedAt;
    std::string               mDefaultLayoutName;
    std::string               mDefaultLayoutSourcePath;

    // ── Engine ───────────────────────────────────────────────────────────
    std::unique_ptr<EngineSession> mSession;
    AppState      mState      = AppState::Idle;
    std::string   mLastError;
    bool          mShutdownRequested = false;

    // Status snapshot (updated each frame when running)
    EngineStatus  mStatus{};

    // ── Input configuration ───────────────────────────────────────────────
    std::string mSourcePath;   // ADM WAV file or LUSID package directory
    std::string mLayoutPath;   // speaker layout JSON
    std::string              mDeviceName;   // "" = system default
    std::vector<std::string> mDeviceList;  // populated by scanDevices(); [0] = system default
    int                      mDeviceIdx = 0;
    int         mBufferSizeIdx = 3;   // index into kBufferSizes[]
    int         mLayoutPreset  = 0;   // index into kLayoutNames[]

    // Source type (re-evaluated whenever mSourcePath changes)
    bool        mSourceIsAdm   = false;
    bool        mSourceIsLusid = false;
    std::string mSourceHint;

    // ── Runtime controls ──────────────────────────────────────────────────
    float mGainDb        = 0.0f;   // Master gain in dB (-60–+12, 0 = unity)
    float mFocus         = 1.5f;
    float mSpkMixDb      = 0.0f;   // Speaker mix trim in dB (-60–+12)
    float mSubMixDb      = 0.0f;   // Sub mix trim in dB (-60–+12)
    int   mElevationMode = 0;    // 0=RescaleAtmosUp, 1=RescaleFullSphere, 2=Clamp

    // ── cult-transcoder ADM flow ──────────────────────────────────────────
    SubprocessRunner mTranscoder;
    std::string      mTranscodeScene;  // output scene.lusid.json path
    std::string      mTranscodeAdm;    // original ADM WAV path (for loadScene)
    std::optional<std::filesystem::path> mActiveTempSessionRoot;
    TempSessionManifest     mActiveTempManifest;
    bool                    mLastGeneratedSceneAvailable = false;
    bool                    mLastFailureHasDiagnostics = false;

    // ── Engine log ────────────────────────────────────────────────────────
    std::deque<LogEntry> mEngineLog;
    bool                 mEngineLogAutoScroll = true;
    // Note: mEngineLog is only written from the main thread.

    // ── Offline Render panel state ────────────────────────────────────────

    // ADM WAV mode fields
    std::string      mOrAdmInput;           // ADM WAV path
    std::string      mOrLayout;             // speaker layout JSON
    int              mOrLayoutPreset  = 0;  // index into kLayoutNames[]
    std::string      mOrOutput;             // output WAV path
    std::string      mOrCultOverride;       // optional --cult-transcoder path
    bool             mOrKeepTempDir = false;

    // LUSID package mode fields
    std::string      mOrLusidPackage;          // directory containing scene.lusid.json + stems
    std::string      mOrLusidLayout;           // speaker layout JSON
    int              mOrLusidLayoutPreset = 0; // index into kLayoutNames[]
    std::string      mOrLusidOutput;           // output WAV path

    SubprocessRunner mOrRunner;
    bool             mOrRunning  = false;
    bool             mOrDone     = false;
    bool             mOrSuccess  = false;

    // Offline render log — written from mOrRunner background thread → needs mutex.
    std::deque<LogEntry> mOrLog;
    std::mutex           mOrLogMutex;
    bool                 mOrLogAutoScroll = true;

    // ── Transcode panel state ─────────────────────────────────────────────
    int              mTcWorkflow = 0;   // 0=ADM/BW64→LUSID, 1=LUSID→ADM/BW64

    // Workflow 0: ADM/BW64 to LUSID (unifies transcode + package-adm-wav)
    std::string      mTcInput;          // input file path
    std::string      mTcOutput;         // output path (json or package dir)
    int              mTcInFormat = 0;    // 0=auto 1=adm_wav 2=adm_xml
    int              mTcLfeMode  = 0;   // 0=hardcoded 1=speaker-label
    int              mTcOutputType = 0; // 0=Scene JSON only, 1=Full LUSID package

    // Workflow 1: LUSID to ADM/BW64 Export (cult-transcoder adm-author)
    int              mTcAdmInputMode = 0;       // 0=scene+wav-dir  1=lusid-package
    std::string      mTcAdmLusid;               // scene.lusid.json (mode 0)
    std::string      mTcAdmWavDir;              // wav directory (mode 0)
    std::string      mTcAdmLusidPkg;            // lusid-package directory (mode 1)
    std::string      mTcAdmOutXml;              // output .adm.xml
    std::string      mTcAdmOutWav;              // output .wav
    std::string      mTcAdmDbmdSrc;             // experimental: dbmd source path
    bool             mTcAdmMetadataPostData = false;  // experimental chunk reorder

    SubprocessRunner mTcRunner;
    bool             mTcRunning  = false;
    bool             mTcSuccess  = false;
    bool             mTcDone     = false;
    std::string      mTcStatusDetail;
    std::string      mTcExpectedPrimaryOutput;
    std::string      mTcExpectedSecondaryOutput;
    std::string      mTcExpectedReportPath;

    // Transcode log is written from mTcRunner background thread → needs mutex.
    std::deque<LogEntry> mTcLog;
    std::mutex           mTcLogMutex;
    bool                 mTcLogAutoScroll = true;
    std::optional<std::filesystem::path> mTcTempSessionRoot;
    TempSessionManifest     mTcTempManifest;

    // ── Logo texture (loaded once at startup; 0 = not loaded, ⊙ fallback used) ──
    unsigned int mLogoTexId = 0;  // GLuint — avoids pulling GL headers into App.hpp

    // ── Static constants ─────────────────────────────────────────────────
    static constexpr int kBufferSizes[]      = {64, 128, 256, 512, 1024};
    static constexpr const char* kBufferSizeNames[] =
        {"64", "128", "256", "512", "1024"};
    static constexpr const char* kLayoutNames[] =
        {"AlloSphere", "Translab", "Stereo", "Krakow", "Layout Template",
         "Circle 16", "Circle 12", "Cube 8", "Ring12 Top4", "Ring8 Top4",
         "Dual Ring 16", "Octagon 8", "Quad 4", "Hexagon 6", "5.1",
         "Custom"};
    static constexpr const char* kLayoutPaths[] = {
        "source/speaker_layouts/allosphere_layout.json",
        "source/speaker_layouts/translab-sono-layout.json",
        "source/speaker_layouts/stereo.json",
        "source/speaker_layouts/krakow_layout.json",
        "source/speaker_layouts/layout_template.json",
        "source/speaker_layouts/example_layouts/circle_16.json",
        "source/speaker_layouts/example_layouts/circle_12.json",
        "source/speaker_layouts/example_layouts/cube_8.json",
        "source/speaker_layouts/example_layouts/ring12_top4.json",
        "source/speaker_layouts/example_layouts/ring8_top4.json",
        "source/speaker_layouts/example_layouts/dual_ring_16.json",
        "source/speaker_layouts/example_layouts/octagon_8.json",
        "source/speaker_layouts/example_layouts/quad_4.json",
        "source/speaker_layouts/example_layouts/hexagon_6.json",
        "source/speaker_layouts/example_layouts/5_1.json",
        ""
    };
    static constexpr const char* kElevModeNames[] = {
        "Rescale Atmos Up (0-90 deg)",
        "Rescale Full Sphere (+-90 deg)",
        "Clamp to Layout"
    };
    static constexpr const char* kTcWorkflowNames[] = {
        "ADM/BW64 to LUSID",
        "LUSID to ADM/BW64"
    };
    static constexpr const char* kTcOutputTypeNames[] = {
        "Scene JSON only",
        "Full LUSID package"
    };
    static constexpr const char* kTcFormatNames[] =
        {"Auto-detect", "ADM WAV", "ADM XML"};
    static constexpr const char* kTcFormatValues[] =
        {"adm_wav", "adm_wav", "adm_xml"};  // [0] auto resolves at runtime from extension
    static constexpr const char* kTcLfeModeNames[]  = {"Hardcoded (default)", "Speaker Label"};
    static constexpr const char* kTcLfeModeValues[] = {"hardcoded", "speaker-label"};
    static constexpr const char* kTcAdmInputModeNames[] = {
        "Scene JSON + WAV Directory",
        "LUSID Package Directory"
    };

    // ── Private methods ───────────────────────────────────────────────────

    // Per-frame engine update (runs when Running or Paused)
    void tickEngine();

    // UI rendering
    void renderUI();
    void renderEngineTab();
    void renderTranscodeTab();
    void renderOfflineRenderTab();

    // Engine lifecycle helpers
    void onStart();
    void onStop();
    void onPause();
    void onResume();
    void doLaunchEngine(const std::string& scenePath,
                        const std::string& sourcesFolder,
                        const std::string& admFile);
    void resetRuntimeToDefaults();

    // Source type detection (updates mSourceIsAdm, mSourceIsLusid, mSourceHint)
    void detectSource();

    // Audio device enumeration (populates mDeviceList via al::AudioDevice)
    void scanDevices();

    // Path helpers
    std::string resolveProjectPath(const std::string& relPath) const;
    std::string findCultTranscoder() const;
    std::string findSpatialRenderer() const;
    std::filesystem::path tempSessionsRoot() const;
    std::filesystem::path createOwnedTempSession(const std::string& sessionType,
                                                 const std::string& sourcePath,
                                                 TempSessionManifest& manifestOut);
    void                  updateManifest(const std::filesystem::path& sessionRoot, TempSessionManifest& manifest,
                                         const std::string& status, bool saved, bool preserved);
    bool                  saveGeneratedSceneCopy(const std::filesystem::path& sessionRoot,
                                                 TempSessionManifest& manifest,
                                                 const std::string& dialogTitle,
                                                 const std::string& successLabel);
    bool                  saveDiagnosticsCopy(const std::filesystem::path& sessionRoot,
                                              TempSessionManifest& manifest,
                                              const std::string& dialogTitle,
                                              const std::string& successLabel);
    void         cleanupOwnedTempSessions(bool forceNow = false);
    void         clearTempSessionState();
    void         clearStandaloneTranscodeTempState();
    static std::string pathString(const std::filesystem::path& path);

    // Default layout helpers
    void tryLoadDefaultLayoutOnStartup();
    void onSetAsDefaultLayout();
    void onClearDefaultLayout();
    void renderDefaultLayoutControls();

    // Log helpers (main thread only for mEngineLog)
    void appendEngineLog(const std::string& text,
                         ImVec4 color = {0.85f, 0.85f, 0.85f, 1.f});
    // Thread-safe (called from mOrRunner background thread)
    void appendOrLog(const std::string& line);
    // Appends a pre-formatted failure diagnostic block (from getFailureDiagnostics())
    // to the engine log, splitting on newlines and colour-coding header/body lines.
    void appendFailureDiagnostics(const std::string& diagnostics);
    // Thread-safe (called from mTcRunner background thread)
    void appendTcLog(const std::string& line);

    // State display helpers
    static const char*  stateName(AppState s);
    static ImVec4       stateColor(AppState s);
};
