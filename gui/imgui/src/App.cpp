// App.cpp — Spatial Root ImGui + GLFW desktop GUI application implementation.
//
// Feature parity target: gui/realtimeGUI/ (Python PySide6 GUI).
// Controls the engine via direct C++ API calls (EngineSession setters) rather
// than OSC, matching the Stage 2 embedding architecture.

#include "App.hpp"
#include "FileDialog.hpp"
#include "imgui_stdlib.h"    // ImGui::InputText with std::string

#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace fs = std::filesystem;

// ── Static constants (out-of-class definitions) ───────────────────────────────
constexpr int         App::kBufferSizes[];
constexpr const char* App::kBufferSizeNames[];
constexpr const char* App::kLayoutNames[];
constexpr const char* App::kLayoutPaths[];
constexpr const char* App::kElevModeNames[];
constexpr const char* App::kTcFormatNames[];
constexpr const char* App::kTcFormatValues[];
constexpr const char* App::kTcLfeModeNames[];
constexpr const char* App::kTcLfeModeValues[];

// ── Constructor / Destructor ──────────────────────────────────────────────────

App::App(std::string projectRoot)
    : mProjectRoot(std::move(projectRoot))
{
    // Pre-populate layout path from the default preset
    mLayoutPath = resolveProjectPath(kLayoutPaths[0]);

    appendEngineLog("[GUI] Spatial Root — ImGui + GLFW GUI started.");
    appendEngineLog("[GUI] Project root: " + mProjectRoot);
    appendEngineLog("[GUI] Select a source and layout, then click START.");
}

App::~App() {
    requestShutdown();
}

// ── tick() — called once per render frame ─────────────────────────────────────

void App::tick() {
    tickEngine();
    renderUI();
}

void App::tickEngine() {
    // Poll the running engine
    if (mState == AppState::Running || mState == AppState::Paused) {
        mStatus = mSession.queryStatus();
        mSession.update();

        DiagnosticEvents ev = mSession.consumeDiagnostics();

        // Log diagnostic events
        char buf[256];
        if (ev.renderRelocEvent) {
            snprintf(buf, sizeof(buf),
                "[RELOC-RENDER]  t=%.1fs  0x%llx → 0x%llx",
                mStatus.timeSec,
                (unsigned long long)ev.renderRelocPrev,
                (unsigned long long)ev.renderRelocNext);
            appendEngineLog(buf, {0.7f, 0.7f, 1.f, 1.f});
        }
        if (ev.deviceRelocEvent) {
            snprintf(buf, sizeof(buf),
                "[RELOC-DEVICE]  t=%.1fs  0x%llx → 0x%llx",
                mStatus.timeSec,
                (unsigned long long)ev.deviceRelocPrev,
                (unsigned long long)ev.deviceRelocNext);
            appendEngineLog(buf, {0.7f, 0.7f, 1.f, 1.f});
        }
        if (ev.renderDomRelocEvent && mStatus.mainRms > 0.005f) {
            snprintf(buf, sizeof(buf),
                "[DOM-RENDER]    t=%.1fs  0x%llx → 0x%llx",
                mStatus.timeSec,
                (unsigned long long)ev.renderDomRelocPrev,
                (unsigned long long)ev.renderDomRelocNext);
            appendEngineLog(buf, {0.6f, 0.8f, 1.f, 1.f});
        }
        if (ev.renderClusterEvent && mStatus.mainRms > 0.005f) {
            snprintf(buf, sizeof(buf),
                "[CLUSTER-RENDER] t=%.1fs  0x%llx → 0x%llx",
                mStatus.timeSec,
                (unsigned long long)ev.renderClusterPrev,
                (unsigned long long)ev.renderClusterNext);
            appendEngineLog(buf, {0.6f, 0.8f, 1.f, 1.f});
        }

        // Device loss / exit request
        if (mStatus.isExitRequested) {
            appendEngineLog("[Engine] Exit requested (device loss?). Shutting down.",
                            {1.f, 0.5f, 0.2f, 1.f});
            mSession.shutdown();
            mState = AppState::Idle;
        }

        // Sync pause state
        if (mStatus.paused && mState == AppState::Running)
            mState = AppState::Paused;
        else if (!mStatus.paused && mState == AppState::Paused)
            mState = AppState::Running;
    }

    // Check if ADM transcoder finished
    if (mState == AppState::Transcoding && !mTranscoder.isRunning()) {
        int code = mTranscoder.exitCode();
        if (code == 0) {
            appendEngineLog("[Transcoder] Complete. Launching engine...",
                            {0.3f, 0.9f, 0.3f, 1.f});
            doLaunchEngine(mTranscodeScene, "", mTranscodeAdm);
        } else {
            mLastError = "cult-transcoder exited with code " + std::to_string(code);
            appendEngineLog("[Transcoder] FAILED: " + mLastError, {1.f, 0.3f, 0.3f, 1.f});
            mState = AppState::Error;
        }
    }

    // Check if standalone transcode panel runner finished
    bool tcWasRunning = mTcRunning;
    mTcRunning = mTcRunner.isRunning();
    if (tcWasRunning && !mTcRunning) {
        mTcDone    = true;
        mTcSuccess = (mTcRunner.exitCode() == 0);
    }
}

// ── requestShutdown() ─────────────────────────────────────────────────────────

void App::requestShutdown() {
    if (mShutdownRequested) return;
    mShutdownRequested = true;
    if (mState == AppState::Running || mState == AppState::Paused) {
        appendEngineLog("[GUI] Shutting down engine...");
        mSession.shutdown();
        mState = AppState::Idle;
    }
}

// ── renderUI() ────────────────────────────────────────────────────────────────

void App::renderUI() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##root", nullptr,
        ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ── Header bar ────────────────────────────────────────────────────────
    ImGui::TextDisabled("Spatial Root");
    ImGui::SameLine();
    ImGui::Text("Real-Time Engine");
    ImGui::SameLine(ImGui::GetWindowWidth() - 200.f);
    ImGui::TextColored(stateColor(mState), "● %s", stateName(mState));

    ImGui::Separator();

    // ── Tabs ──────────────────────────────────────────────────────────────
    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("ENGINE")) {
            renderEngineTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("TRANSCODE")) {
            renderTranscodeTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ── renderEngineTab() ─────────────────────────────────────────────────────────

void App::renderEngineTab() {
    bool isRunning = (mState == AppState::Running || mState == AppState::Paused);
    bool isIdle    = (mState == AppState::Idle || mState == AppState::Error);

    // ── INPUT CONFIGURATION ───────────────────────────────────────────────
    ImGui::SeparatorText("INPUT CONFIGURATION");

    // Source path
    ImGui::TextDisabled("SOURCE");
    ImGui::SameLine(120.f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
    if (ImGui::InputText("##source", &mSourcePath)) detectSource();
    ImGui::SameLine();
    if (ImGui::Button("Browse##src")) {
        std::string p = pickFileOrDirectory("Select Audio Source");
        if (!p.empty()) { mSourcePath = p; detectSource(); }
    }
    // Hint text (below source row)
    if (!mSourceHint.empty()) {
        ImVec4 hintCol = (mSourceIsAdm || mSourceIsLusid)
            ? ImVec4{0.3f, 0.9f, 0.3f, 1.f}
            : ImVec4{1.f, 0.4f, 0.4f, 1.f};
        ImGui::SameLine(120.f);
        ImGui::TextColored(hintCol, "%s", mSourceHint.c_str());
    }

    // Speaker layout
    ImGui::TextDisabled("LAYOUT");
    ImGui::SameLine(120.f);
    ImGui::SetNextItemWidth(110.f);
    if (ImGui::Combo("##layoutpreset", &mLayoutPreset, kLayoutNames, 3)) {
        if (mLayoutPreset < 2)
            mLayoutPath = resolveProjectPath(kLayoutPaths[mLayoutPreset]);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
    ImGui::InputText("##layout", &mLayoutPath);
    ImGui::SameLine();
    if (ImGui::Button("Browse##layout")) {
        std::string p = pickFile("Select Speaker Layout", {"*.json"}, "JSON files");
        if (!p.empty()) { mLayoutPath = p; mLayoutPreset = 2; }  // switch to Custom
    }

    // Remap CSV (optional)
    ImGui::TextDisabled("REMAP CSV");
    ImGui::SameLine(120.f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
    ImGui::InputText("##remap", &mRemapPath);
    ImGui::SameLine();
    if (ImGui::Button("Browse##remap")) {
        std::string p = pickFile("Select Remap CSV", {"*.csv"}, "CSV files");
        if (!p.empty()) mRemapPath = p;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(optional)");

    // Output device
    ImGui::TextDisabled("DEVICE");
    ImGui::SameLine(120.f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 8.f);
    ImGui::InputText("##device", &mDeviceName);
    ImGui::SameLine(120.f);
    ImGui::TextDisabled("(empty = system default; use --list-devices to enumerate)");

    // Buffer size
    ImGui::TextDisabled("BUFFER");
    ImGui::SameLine(120.f);
    ImGui::SetNextItemWidth(100.f);
    ImGui::Combo("##bufsize", &mBufferSizeIdx, kBufferSizeNames, 5);

    if (isRunning) {
        // Lock inputs while engine is running
        ImGui::BeginDisabled(true);
        ImGui::Text("  (inputs locked while engine is running)");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    // ── TRANSPORT ─────────────────────────────────────────────────────────
    ImGui::SeparatorText("TRANSPORT");

    {
        bool canStart  = isIdle;
        bool canStop   = isRunning;
        bool canPause  = (mState == AppState::Running);
        bool canResume = (mState == AppState::Paused);
        bool busy      = (mState == AppState::Transcoding);

        if (!canStart) ImGui::BeginDisabled(true);
        if (ImGui::Button("START")) onStart();
        if (!canStart) ImGui::EndDisabled();

        ImGui::SameLine();
        if (!canStop) ImGui::BeginDisabled(true);
        if (ImGui::Button("STOP")) onStop();
        if (!canStop) ImGui::EndDisabled();

        ImGui::SameLine();
        if (!canPause) ImGui::BeginDisabled(true);
        if (ImGui::Button("PAUSE")) onPause();
        if (!canPause) ImGui::EndDisabled();

        ImGui::SameLine();
        if (!canResume) ImGui::BeginDisabled(true);
        if (ImGui::Button("RESUME")) onResume();
        if (!canResume) ImGui::EndDisabled();

        if (busy) {
            ImGui::SameLine();
            ImGui::TextColored({1.f, 0.8f, 0.f, 1.f}, "Transcoding ADM...");
        }

        // Error message
        if (mState == AppState::Error && !mLastError.empty()) {
            ImGui::TextColored({1.f, 0.3f, 0.3f, 1.f}, "Error: %s", mLastError.c_str());
        }
    }

    ImGui::Spacing();

    // ── STATUS (only when engine is running) ──────────────────────────────
    if (isRunning) {
        ImGui::SeparatorText("STATUS");
        ImGui::Text("t=%.1fs  CPU=%.1f%%  mainRms=%.4f  subRms=%.4f  Xrun=%zu",
            mStatus.timeSec,
            mStatus.cpuLoad * 100.f,
            mStatus.mainRms,
            mStatus.subRms,
            mStatus.xruns);
        ImGui::SameLine();
        if (mStatus.paused)
            ImGui::TextColored({1.f, 0.6f, 0.f, 1.f}, "  PAUSED");
        else
            ImGui::TextColored({0.2f, 0.9f, 0.2f, 1.f}, "  PLAYING");

        if (mStatus.nanGuardCount > 0 || mStatus.speakerProximityCount > 0) {
            ImGui::TextColored({1.f, 0.7f, 0.2f, 1.f},
                "  NaN-guard=%llu  Prox=%llu",
                (unsigned long long)mStatus.nanGuardCount,
                (unsigned long long)mStatus.speakerProximityCount);
        }
        ImGui::Spacing();
    }

    // ── RUNTIME CONTROLS ──────────────────────────────────────────────────
    ImGui::SeparatorText("RUNTIME CONTROLS");

    if (!isRunning) {
        ImGui::TextDisabled("  (controls active after START)");
        ImGui::BeginDisabled(true);
    }

    // Master Gain
    ImGui::TextDisabled("Master Gain");
    ImGui::SameLine(160.f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.f);
    if (ImGui::SliderFloat("##gain", &mGain, 0.1f, 3.0f, "%.2f"))
        if (isRunning) mSession.setMasterGain(mGain);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.f);
    if (ImGui::InputFloat("##gaininput", &mGain, 0.f, 0.f, "%.2f"))
        if (isRunning) { mGain = std::clamp(mGain, 0.1f, 3.0f); mSession.setMasterGain(mGain); }

    // DBAP Focus
    ImGui::TextDisabled("DBAP Focus");
    ImGui::SameLine(160.f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.f);
    if (ImGui::SliderFloat("##focus", &mFocus, 0.2f, 5.0f, "%.2f"))
        if (isRunning) mSession.setDbapFocus(mFocus);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.f);
    if (ImGui::InputFloat("##focusinput", &mFocus, 0.f, 0.f, "%.2f"))
        if (isRunning) { mFocus = std::clamp(mFocus, 0.2f, 5.0f); mSession.setDbapFocus(mFocus); }

    // Speaker Mix dB
    ImGui::TextDisabled("Speaker Mix dB");
    ImGui::SameLine(160.f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.f);
    if (ImGui::SliderFloat("##spkmix", &mSpkMixDb, -10.f, 10.f, "%.1f dB"))
        if (isRunning) mSession.setSpeakerMixDb(mSpkMixDb);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.f);
    if (ImGui::InputFloat("##spkmixinput", &mSpkMixDb, 0.f, 0.f, "%.1f"))
        if (isRunning) { mSpkMixDb = std::clamp(mSpkMixDb, -10.f, 10.f); mSession.setSpeakerMixDb(mSpkMixDb); }

    // Sub Mix dB
    ImGui::TextDisabled("Sub Mix dB");
    ImGui::SameLine(160.f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.f);
    if (ImGui::SliderFloat("##submix", &mSubMixDb, -10.f, 10.f, "%.1f dB"))
        if (isRunning) mSession.setSubMixDb(mSubMixDb);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.f);
    if (ImGui::InputFloat("##submixinput", &mSubMixDb, 0.f, 0.f, "%.1f"))
        if (isRunning) { mSubMixDb = std::clamp(mSubMixDb, -10.f, 10.f); mSession.setSubMixDb(mSubMixDb); }

    // Auto Compensation
    if (ImGui::Checkbox("Focus Auto-Compensation", &mAutoComp))
        if (isRunning) mSession.setAutoCompensation(mAutoComp);

    // Elevation Mode
    ImGui::TextDisabled("Elevation Mode");
    ImGui::SameLine(160.f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 8.f);
    if (ImGui::Combo("##elevmode", &mElevationMode, kElevModeNames, 3))
        if (isRunning) mSession.setElevationMode(static_cast<ElevationMode>(mElevationMode));

    if (!isRunning) ImGui::EndDisabled();

    ImGui::Spacing();

    // ── ENGINE LOG ────────────────────────────────────────────────────────
    ImGui::SeparatorText("ENGINE LOG");
    ImGui::SetNextWindowBgAlpha(0.5f);
    if (ImGui::BeginChild("##enginelog",
            ImVec2(0.f, ImGui::GetContentRegionAvail().y - 8.f),
            false,
            ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const auto& entry : mEngineLog) {
            ImGui::TextColored(entry.color, "%s", entry.text.c_str());
        }
        if (mEngineLogAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.f)
            ImGui::SetScrollHereY(1.f);
    }
    ImGui::EndChild();
}

// ── renderTranscodeTab() ──────────────────────────────────────────────────────

void App::renderTranscodeTab() {
    ImGui::SeparatorText("ADM -> LUSID TRANSCODE");
    ImGui::TextDisabled("Run cult-transcoder to convert an ADM WAV/XML to a LUSID JSON package.");
    ImGui::Spacing();

    // Input file
    ImGui::TextDisabled("INPUT FILE");
    ImGui::SameLine(130.f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
    ImGui::InputText("##tcinput", &mTcInput);
    ImGui::SameLine();
    if (ImGui::Button("Browse##tcinput")) {
        std::string p = pickFile("Select ADM Input", {"*.wav", "*.xml"}, "ADM files");
        if (!p.empty()) mTcInput = p;
    }

    // In-format override
    ImGui::TextDisabled("IN-FORMAT");
    ImGui::SameLine(130.f);
    ImGui::SetNextItemWidth(140.f);
    ImGui::Combo("##tcinformat", &mTcInFormat, kTcFormatNames, 4);

    // LFE mode
    ImGui::TextDisabled("LFE MODE");
    ImGui::SameLine(130.f);
    ImGui::SetNextItemWidth(140.f);
    ImGui::Combo("##tclfemode", &mTcLfeMode, kTcLfeModeNames, 2);

    // Output path
    ImGui::TextDisabled("OUTPUT");
    ImGui::SameLine(130.f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 8.f);
    ImGui::InputText("##tcoutput", &mTcOutput);
    ImGui::SameLine(130.f);
    ImGui::TextDisabled("(empty = auto-derived from input path)");

    ImGui::Spacing();

    // TRANSCODE button + status
    bool tcBusy = mTcRunner.isRunning();
    if (tcBusy) ImGui::BeginDisabled(true);
    if (ImGui::Button("TRANSCODE", {140.f, 0.f})) {
        // Resolve format (auto = infer from extension)
        std::string format = kTcFormatValues[mTcInFormat];
        if (mTcInFormat == 0) {
            // Auto-detect from extension
            std::string lower = mTcInput;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".wav")
                format = "adm_wav";
            else if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".xml")
                format = "adm_xml";
            else
                format = "adm_wav";  // safe default
        }

        // Resolve output path
        std::string outPath = mTcOutput;
        if (outPath.empty() && !mTcInput.empty()) {
            fs::path p(mTcInput);
            outPath = (p.parent_path() / (p.stem().string() + ".lusid.json")).string();
        }

        std::string cultBin = findCultTranscoder();
        if (cultBin.empty()) {
            appendTcLog("[error] cult-transcoder binary not found. Build with ./build.sh first.");
        } else if (mTcInput.empty()) {
            appendTcLog("[error] Input path is required.");
        } else {
            mTcDone = false;
            mTcSuccess = false;
            mTcRunning = true;

            // Derive report path
            fs::path op(outPath);
            std::string reportPath = (op.parent_path() /
                (op.stem().string() + "_report.json")).string();

            // Create output directory
            try { fs::create_directories(op.parent_path()); } catch (...) {}

            std::vector<std::string> args = {
                cultBin,
                "transcode",
                "--in",         mTcInput,
                "--in-format",  format,
                "--out",        outPath,
                "--out-format", "lusid_json",
                "--report",     reportPath,
                "--lfe-mode",   kTcLfeModeValues[mTcLfeMode],
            };
            appendTcLog("[GUI] Running: " + cultBin + " transcode ...");

            mTcRunner.start(args, [this](const std::string& line) {
                appendTcLog(line);
            });
        }
    }
    if (tcBusy) ImGui::EndDisabled();

    ImGui::SameLine();
    if (mTcDone) {
        if (mTcSuccess)
            ImGui::TextColored({0.3f, 0.9f, 0.3f, 1.f}, "COMPLETE");
        else
            ImGui::TextColored({1.f, 0.3f, 0.3f, 1.f}, "FAILED");
    } else if (tcBusy) {
        ImGui::TextColored({1.f, 0.8f, 0.f, 1.f}, "Running...");
    } else {
        ImGui::TextDisabled("IDLE");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("TRANSCODE LOG");

    // Drain the thread-safe log into rendering
    {
        std::lock_guard<std::mutex> lock(mTcLogMutex);
        // log is already in mTcLog — no copy needed since we render under lock
        ImGui::SetNextWindowBgAlpha(0.5f);
        if (ImGui::BeginChild("##tclog",
                ImVec2(0.f, ImGui::GetContentRegionAvail().y - 8.f),
                false,
                ImGuiWindowFlags_HorizontalScrollbar)) {
            for (const auto& entry : mTcLog) {
                ImGui::TextColored(entry.color, "%s", entry.text.c_str());
            }
            if (mTcLogAutoScroll &&
                ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.f)
                ImGui::SetScrollHereY(1.f);
        }
        ImGui::EndChild();
    }
}

// ── Engine lifecycle ──────────────────────────────────────────────────────────

void App::onStart() {
    if (mSourcePath.empty()) {
        mLastError = "Source path is required.";
        mState = AppState::Error;
        appendEngineLog("[GUI] Cannot start: " + mLastError, {1.f, 0.4f, 0.4f, 1.f});
        return;
    }
    if (mLayoutPath.empty()) {
        mLastError = "Layout path is required.";
        mState = AppState::Error;
        appendEngineLog("[GUI] Cannot start: " + mLastError, {1.f, 0.4f, 0.4f, 1.f});
        return;
    }

    detectSource();

    if (!mSourceIsAdm && !mSourceIsLusid) {
        mLastError = "Source must be an ADM WAV file or a LUSID package directory (containing scene.lusid.json).";
        mState = AppState::Error;
        appendEngineLog("[GUI] Cannot start: " + mLastError, {1.f, 0.4f, 0.4f, 1.f});
        return;
    }

    // Reset controls to defaults on each new start (mirrors Python GUI behaviour)
    resetRuntimeToDefaults();
    mLastError.clear();

    if (mSourceIsAdm) {
        // ADM WAV → run cult-transcoder first
        std::string cultBin = findCultTranscoder();
        if (cultBin.empty()) {
            mLastError = "cult-transcoder binary not found. Build with ./build.sh first.";
            mState = AppState::Error;
            appendEngineLog("[GUI] " + mLastError, {1.f, 0.4f, 0.4f, 1.f});
            return;
        }

        mTranscodeAdm   = mSourcePath;
        mTranscodeScene = transcodeOutputPath(mSourcePath);

        // Create output directory
        try {
            fs::create_directories(fs::path(mTranscodeScene).parent_path());
        } catch (...) {}

        fs::path sp(mTranscodeScene);
        std::string reportPath = (sp.parent_path() /
            (sp.stem().string() + "_report.json")).string();

        std::vector<std::string> args = {
            cultBin,
            "transcode",
            "--in",         mTranscodeAdm,
            "--in-format",  "adm_wav",
            "--out",        mTranscodeScene,
            "--out-format", "lusid_json",
            "--report",     reportPath,
            "--lfe-mode",   "hardcoded",
        };

        appendEngineLog("[GUI] ADM source detected. Running cult-transcoder...",
                        {1.f, 0.8f, 0.2f, 1.f});
        mState = AppState::Transcoding;

        mTranscoder.start(args, [this](const std::string& line) {
            appendEngineLog("[transcoder] " + line);
        });

    } else {
        // LUSID package — find scene.lusid.json
        std::string scenePath = (fs::path(mSourcePath) / "scene.lusid.json").string();
        doLaunchEngine(scenePath, mSourcePath, "");
    }
}

void App::onStop() {
    if (mState == AppState::Running || mState == AppState::Paused) {
        appendEngineLog("[GUI] Stopping engine...");
        mSession.shutdown();
        mState = AppState::Idle;
        appendEngineLog("[GUI] Engine stopped.");
    }
}

void App::onPause() {
    if (mState == AppState::Running) {
        mSession.setPaused(true);
        mState = AppState::Paused;
        appendEngineLog("[GUI] Paused.");
    }
}

void App::onResume() {
    if (mState == AppState::Paused) {
        mSession.setPaused(false);
        mState = AppState::Running;
        appendEngineLog("[GUI] Resumed.");
    }
}

void App::doLaunchEngine(const std::string& scenePath,
                          const std::string& sourcesFolder,
                          const std::string& admFile) {
    appendEngineLog("[GUI] Configuring engine...");

    EngineOptions opts;
    opts.sampleRate    = 48000;
    opts.bufferSize    = kBufferSizes[mBufferSizeIdx];
    opts.outputDeviceName = mDeviceName;
    opts.oscPort       = 9009;  // OSC enabled by default (see App.hpp DEV NOTE)
    opts.elevationMode = ElevationMode::RescaleAtmosUp;

    if (!mSession.configureEngine(opts)) {
        mLastError = mSession.getLastError();
        mState = AppState::Error;
        appendEngineLog("[Engine] configureEngine failed: " + mLastError, {1.f,0.4f,0.4f,1.f});
        return;
    }

    SceneInput scene;
    scene.scenePath     = scenePath;
    scene.sourcesFolder = sourcesFolder;
    scene.admFile       = admFile;

    if (!mSession.loadScene(scene)) {
        mLastError = mSession.getLastError();
        mState = AppState::Error;
        appendEngineLog("[Engine] loadScene failed: " + mLastError, {1.f,0.4f,0.4f,1.f});
        mSession.shutdown();
        return;
    }

    LayoutInput layout;
    layout.layoutPath   = mLayoutPath;
    layout.remapCsvPath = mRemapPath;

    if (!mSession.applyLayout(layout)) {
        mLastError = mSession.getLastError();
        mState = AppState::Error;
        appendEngineLog("[Engine] applyLayout failed: " + mLastError, {1.f,0.4f,0.4f,1.f});
        mSession.shutdown();
        return;
    }

    RuntimeParams rp;
    rp.masterGain       = mGain;
    rp.dbapFocus        = mFocus;
    rp.speakerMixDb     = mSpkMixDb;
    rp.subMixDb         = mSubMixDb;
    rp.autoCompensation = mAutoComp;

    if (!mSession.configureRuntime(rp)) {
        mLastError = mSession.getLastError();
        mState = AppState::Error;
        appendEngineLog("[Engine] configureRuntime failed: " + mLastError, {1.f,0.4f,0.4f,1.f});
        mSession.shutdown();
        return;
    }

    if (!mSession.start()) {
        mLastError = mSession.getLastError();
        mState = AppState::Error;
        appendEngineLog("[Engine] start() failed: " + mLastError, {1.f,0.4f,0.4f,1.f});
        mSession.shutdown();
        return;
    }

    // Apply elevation mode (start() uses ElevationMode::RescaleAtmosUp from opts;
    // if the user has changed the combo, sync it now via the V1.1 setter)
    mSession.setElevationMode(static_cast<ElevationMode>(mElevationMode));

    mState = AppState::Running;
    appendEngineLog("[Engine] Started successfully. OSC port 9009.",
                    {0.3f, 0.9f, 0.3f, 1.f});
}

void App::resetRuntimeToDefaults() {
    mGain          = 0.5f;
    mFocus         = 1.5f;
    mSpkMixDb      = 0.0f;
    mSubMixDb      = 0.0f;
    mAutoComp      = false;
    mElevationMode = 0;
}

// ── Source detection ──────────────────────────────────────────────────────────

void App::detectSource() {
    mSourceIsAdm   = false;
    mSourceIsLusid = false;
    mSourceHint    = "";

    if (mSourcePath.empty()) return;

    std::error_code ec;
    fs::path p(mSourcePath);

    if (!fs::exists(p, ec)) {
        mSourceHint = "Path does not exist";
        return;
    }

    if (fs::is_regular_file(p, ec)) {
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".wav") {
            mSourceIsAdm = true;
            mSourceHint  = "Detected: ADM WAV";
        } else {
            mSourceHint = "Unrecognized file type — expected .wav (ADM)";
        }
    } else if (fs::is_directory(p, ec)) {
        fs::path scene = p / "scene.lusid.json";
        if (fs::exists(scene, ec)) {
            mSourceIsLusid = true;
            mSourceHint    = "Detected: LUSID package";
        } else {
            mSourceHint = "Directory has no scene.lusid.json — not a LUSID package";
        }
    }
}

// ── Path helpers ──────────────────────────────────────────────────────────────

std::string App::resolveProjectPath(const std::string& relPath) const {
    if (relPath.empty()) return "";
    return (fs::path(mProjectRoot) / relPath).string();
}

std::string App::findCultTranscoder() const {
    // Try new root-cmake build location first, then standalone build fallback
    std::vector<std::string> candidates = {
        resolveProjectPath("build/cult_transcoder/cult-transcoder"),
        resolveProjectPath("cult_transcoder/build/cult-transcoder"),
    };
#ifdef _WIN32
    for (auto& c : candidates) c += ".exe";
#endif
    for (const auto& c : candidates) {
        if (fs::exists(c)) return c;
    }
    return "";  // not found — caller must handle
}

std::string App::transcodeOutputPath(const std::string& admPath) const {
    fs::path p(admPath);
    std::string stem = p.stem().string();
    return resolveProjectPath(
        "processedData/stageForRender/" + stem + ".lusid.json");
}

// ── Log helpers ───────────────────────────────────────────────────────────────

void App::appendEngineLog(const std::string& text, ImVec4 color) {
    mEngineLog.push_back({color, text});
    if (mEngineLog.size() > 2000)
        mEngineLog.pop_front();
}

void App::appendTcLog(const std::string& line) {
    // Called from SubprocessRunner background thread — needs mutex.
    ImVec4 color = {0.85f, 0.85f, 0.85f, 1.f};
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("error") != std::string::npos)
        color = {1.f, 0.35f, 0.35f, 1.f};
    else if (lower.find("warn") != std::string::npos)
        color = {1.f, 0.8f, 0.2f, 1.f};
    else if (line.size() > 3 && line.substr(0, 4) == "[ok]")
        color = {0.3f, 0.9f, 0.3f, 1.f};

    std::lock_guard<std::mutex> lock(mTcLogMutex);
    mTcLog.push_back({color, line});
    if (mTcLog.size() > 2000)
        mTcLog.pop_front();
}

// ── State display helpers ─────────────────────────────────────────────────────

const char* App::stateName(AppState s) {
    switch (s) {
        case AppState::Idle:        return "IDLE";
        case AppState::Transcoding: return "TRANSCODING";
        case AppState::Running:     return "RUNNING";
        case AppState::Paused:      return "PAUSED";
        case AppState::Error:       return "ERROR";
    }
    return "UNKNOWN";
}

ImVec4 App::stateColor(AppState s) {
    switch (s) {
        case AppState::Idle:        return {0.55f, 0.55f, 0.55f, 1.f};
        case AppState::Transcoding: return {1.f,   0.8f,  0.f,   1.f};
        case AppState::Running:     return {0.2f,  0.9f,  0.2f,  1.f};
        case AppState::Paused:      return {1.f,   0.6f,  0.1f,  1.f};
        case AppState::Error:       return {1.f,   0.3f,  0.3f,  1.f};
    }
    return {1.f, 1.f, 1.f, 1.f};
}
