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

#include "EngineSession.hpp"
#include "SubprocessRunner.hpp"
#include "imgui.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
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
    explicit App(std::string projectRoot = ".");
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
    std::string mRemapPath;    // DEPRECATED — CSV remap; remove after layout-routing validation
    std::string              mDeviceName;   // "" = system default
    std::vector<std::string> mDeviceList;  // populated by scanDevices(); [0] = system default
    int                      mDeviceIdx = 0;
    int         mBufferSizeIdx = 3;   // index into kBufferSizes[]
    int         mLayoutPreset  = 0;   // index into kLayoutNames[]

    // Source type (re-evaluated whenever mSourcePath changes)
    bool        mSourceIsAdm   = false;
    bool        mSourceIsLusid = false;
    std::string mSourceHint;

    // ── Runtime controls (match Python GUI / engine defaults) ─────────────
    float mGain          = 0.5f;
    float mFocus         = 1.5f;
    float mSpkMixDb      = 0.0f;
    float mSubMixDb      = 0.0f;
    int   mElevationMode = 0;    // 0=RescaleAtmosUp, 1=RescaleFullSphere, 2=Clamp

    // ── cult-transcoder ADM flow ──────────────────────────────────────────
    SubprocessRunner mTranscoder;
    std::string      mTranscodeScene;  // output scene.lusid.json path
    std::string      mTranscodeAdm;    // original ADM WAV path (for loadScene)

    // ── Engine log ────────────────────────────────────────────────────────
    std::deque<LogEntry> mEngineLog;
    bool                 mEngineLogAutoScroll = true;
    // Note: mEngineLog is only written from the main thread.

    // ── Transcode panel state ─────────────────────────────────────────────
    std::string      mTcInput;          // input file/dir path
    std::string      mTcOutput;         // output .lusid.json (empty = auto)
    int              mTcInFormat = 0;   // 0=auto 1=adm_wav 2=adm_xml 3=lusid_json
    int              mTcLfeMode  = 0;   // 0=hardcoded 1=speaker-label
    SubprocessRunner mTcRunner;
    bool             mTcRunning  = false;
    bool             mTcSuccess  = false;
    bool             mTcDone     = false;

    // Transcode log is written from mTcRunner background thread → needs mutex.
    std::deque<LogEntry> mTcLog;
    std::mutex           mTcLogMutex;
    bool                 mTcLogAutoScroll = true;

    // ── Logo texture (loaded once at startup; 0 = not loaded, ⊙ fallback used) ──
    unsigned int mLogoTexId = 0;  // GLuint — avoids pulling GL headers into App.hpp

    // ── Static constants ─────────────────────────────────────────────────
    static constexpr int kBufferSizes[]      = {64, 128, 256, 512, 1024};
    static constexpr const char* kBufferSizeNames[] =
        {"64", "128", "256", "512", "1024"};
    static constexpr const char* kLayoutNames[] =
        {"AlloSphere", "Translab", "Custom"};
    static constexpr const char* kLayoutPaths[] = {
        "spatial_engine/speaker_layouts/allosphere_layout.json",
        "spatial_engine/speaker_layouts/translab-sono-layout.json",
        ""
    };
    static constexpr const char* kElevModeNames[] = {
        "Rescale Atmos Up (0-90 deg)",
        "Rescale Full Sphere (+-90 deg)",
        "Clamp to Layout"
    };
    static constexpr const char* kTcFormatNames[] =
        {"Auto-detect", "ADM WAV", "ADM XML", "LUSID JSON"};
    static constexpr const char* kTcFormatValues[] =
        {"adm_wav", "adm_wav", "adm_xml", "lusid_json"};  // [0] auto resolves at runtime
    static constexpr const char* kTcLfeModeNames[]  = {"Hardcoded", "Speaker Label"};
    static constexpr const char* kTcLfeModeValues[] = {"hardcoded", "speaker-label"};

    // ── Private methods ───────────────────────────────────────────────────

    // Per-frame engine update (runs when Running or Paused)
    void tickEngine();

    // UI rendering
    void renderUI();
    void renderEngineTab();
    void renderTranscodeTab();

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
    std::string transcodeOutputPath(const std::string& admPath) const;

    // Log helpers (main thread only for mEngineLog)
    void appendEngineLog(const std::string& text,
                         ImVec4 color = {0.85f, 0.85f, 0.85f, 1.f});
    // Thread-safe (called from mTcRunner background thread)
    void appendTcLog(const std::string& line);

    // State display helpers
    static const char*  stateName(AppState s);
    static ImVec4       stateColor(AppState s);
};
