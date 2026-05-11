// App.cpp — Spatial Root ImGui + GLFW desktop GUI application implementation.

#include "App.hpp"
#include "FileDialog.hpp"
#include "imgui_stdlib.h"

#include <al/io/al_AudioIO.hpp>

#include <GLFW/glfw3.h>
#include "miniLogo_data.h"
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>
#include <string>

namespace fs = std::filesystem;

namespace {
bool copyIfPresent(const fs::path& source, const fs::path& destination) {
    std::error_code ec;
    if (!fs::exists(source, ec)) return false;
    fs::create_directories(destination.parent_path());
    if (fs::is_directory(source, ec)) {
        fs::copy(source, destination,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        return true;
    }
    if (fs::is_regular_file(source, ec)) {
        fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
        return true;
    }
    return false;
}

bool hasDiagnosticsFiles(const std::optional<fs::path>& sessionRoot) {
    if (!sessionRoot) return false;
    std::error_code ec;
    return fs::exists(*sessionRoot / "manifest.json", ec) ||
           fs::exists(*sessionRoot / "reports", ec);
}
}

constexpr int         App::kBufferSizes[];
constexpr const char* App::kBufferSizeNames[];
constexpr const char* App::kLayoutNames[];
constexpr const char* App::kLayoutPaths[];
constexpr const char* App::kElevModeNames[];
constexpr const char* App::kTcWorkflowNames[];
constexpr const char* App::kTcFormatNames[];
constexpr const char* App::kTcFormatValues[];
constexpr const char* App::kTcLfeModeNames[];
constexpr const char* App::kTcLfeModeValues[];
constexpr const char* App::kTcAdmInputModeNames[];

App::App(std::string projectRoot, bool keepTempSessions, std::string tempRootOverride)
    : mProjectRoot(std::move(projectRoot))
    , mKeepTempSessions(keepTempSessions)
    , mTempRootOverride(std::move(tempRootOverride))
    , mDefaultLayoutMgr()
    , mSession(std::make_unique<EngineSession>()) {
    mLayoutPath = resolveProjectPath(kLayoutPaths[0]);

    appendEngineLog("[GUI] Spatial Root — ImGui + GLFW GUI started.");
    appendEngineLog("[GUI] Project root: " + mProjectRoot);
    appendEngineLog("[GUI] Temp session root: " + pathString(tempSessionsRoot()));
    if (mKeepTempSessions) {
        appendEngineLog("[GUI] Keeping temporary sessions for debugging is enabled.",
                        {1.f, 0.8f, 0.2f, 1.f});
    }

    tryLoadDefaultLayoutOnStartup();

    appendEngineLog("[GUI] Select a source and layout, then click START.");

    int lw = 0, lh = 0, lch = 0;
    unsigned char* logoData = stbi_load_from_memory(
        miniLogo_png, (int)miniLogo_png_len, &lw, &lh, &lch, 4);
    if (logoData) {
        glGenTextures(1, &mLogoTexId);
        glBindTexture(GL_TEXTURE_2D, mLogoTexId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, logoData);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(logoData);
    }
}

App::~App() {
    requestShutdown();
    if (mLogoTexId != 0) {
        glDeleteTextures(1, &mLogoTexId);
        mLogoTexId = 0;
    }
}

void App::tick() {
    tickEngine();
    renderUI();
}

void App::tickEngine() {
    if (mState == AppState::Running || mState == AppState::Paused) {
        mStatus = mSession->queryStatus();
        mSession->update();

        DiagnosticEvents ev = mSession->consumeDiagnostics();
        char buf[256];
        if (ev.renderRelocEvent) {
            snprintf(buf, sizeof(buf), "[RELOC-RENDER]  t=%.1fs  0x%llx → 0x%llx",
                     mStatus.timeSec,
                     (unsigned long long)ev.renderRelocPrev,
                     (unsigned long long)ev.renderRelocNext);
            appendEngineLog(buf, {0.7f, 0.7f, 1.f, 1.f});
        }
        if (ev.deviceRelocEvent) {
            snprintf(buf, sizeof(buf), "[RELOC-DEVICE]  t=%.1fs  0x%llx → 0x%llx",
                     mStatus.timeSec,
                     (unsigned long long)ev.deviceRelocPrev,
                     (unsigned long long)ev.deviceRelocNext);
            appendEngineLog(buf, {0.7f, 0.7f, 1.f, 1.f});
        }
        if (ev.renderDomRelocEvent && mStatus.mainRms > 0.005f) {
            snprintf(buf, sizeof(buf), "[DOM-RENDER]    t=%.1fs  0x%llx → 0x%llx",
                     mStatus.timeSec,
                     (unsigned long long)ev.renderDomRelocPrev,
                     (unsigned long long)ev.renderDomRelocNext);
            appendEngineLog(buf, {0.6f, 0.8f, 1.f, 1.f});
        }
        if (ev.renderClusterEvent && mStatus.mainRms > 0.005f) {
            snprintf(buf, sizeof(buf), "[CLUSTER-RENDER] t=%.1fs  0x%llx → 0x%llx",
                     mStatus.timeSec,
                     (unsigned long long)ev.renderClusterPrev,
                     (unsigned long long)ev.renderClusterNext);
            appendEngineLog(buf, {0.6f, 0.8f, 1.f, 1.f});
        }
        if (mStatus.isExitRequested) {
            appendEngineLog("[Engine] Exit requested (device loss?). Shutting down.",
                            {1.f, 0.5f, 0.2f, 1.f});
            mSession->shutdown();
            mState = AppState::Idle;
        }
        if (mStatus.paused && mState == AppState::Running) mState = AppState::Paused;
        else if (!mStatus.paused && mState == AppState::Paused) mState = AppState::Running;
    }

    if (mState == AppState::Transcoding && !mTranscoder.isRunning()) {
        const int code = mTranscoder.exitCode();
        if (code == 0) {
            if (mActiveTempSessionRoot) {
                updateManifest(*mActiveTempSessionRoot, mActiveTempManifest, "generated", false, false);
            }
            appendEngineLog("[Transcoder] Complete. Launching engine...", {0.3f, 0.9f, 0.3f, 1.f});
            doLaunchEngine(mTranscodeScene, "", mTranscodeAdm);
        } else {
            mLastError = "cult-transcoder exited with code " + std::to_string(code);
            if (mActiveTempSessionRoot) {
                updateManifest(*mActiveTempSessionRoot, mActiveTempManifest,
                               "failed", false, mKeepTempSessions);
            }
            mLastFailureHasDiagnostics = true;
            appendEngineLog("[Transcoder] FAILED: " + mLastError, {1.f, 0.3f, 0.3f, 1.f});
            mState = AppState::Error;
        }
    }

    const bool tcWasRunning = mTcRunning;
    mTcRunning = mTcRunner.isRunning();
    if (tcWasRunning && !mTcRunning) {
        mTcDone = true;
        mTcSuccess = (mTcRunner.exitCode() == 0);
        if (mTcTempSessionRoot) {
            updateManifest(*mTcTempSessionRoot, mTcTempManifest,
                           mTcSuccess ? "complete" : "failed",
                           false, mKeepTempSessions);
            if (!mTcSuccess) mLastFailureHasDiagnostics = true;
        }
    }

    const bool orWasRunning = mOrRunning;
    mOrRunning = mOrRunner.isRunning();
    if (orWasRunning && !mOrRunning) {
        mOrDone    = true;
        mOrSuccess = (mOrRunner.exitCode() == 0);
        if (mOrSuccess) {
            appendOrLog("[GUI] Offline render complete. Exit code 0.");
        } else {
            appendOrLog("[GUI] Offline render FAILED. Exit code " +
                        std::to_string(mOrRunner.exitCode()) +
                        ". Check log above for details.");
        }
    }
}

void App::requestShutdown() {
    if (mShutdownRequested) return;
    mShutdownRequested = true;
    if (mState == AppState::Running || mState == AppState::Paused) {
        appendEngineLog("[GUI] Shutting down engine...");
        mSession->shutdown();
        mState = AppState::Idle;
    }
    if (mState == AppState::Transcoding && mTranscoder.isRunning()) {
        appendEngineLog("[GUI] Waiting for active transcode to finish before cleanup...",
                        {1.f, 0.8f, 0.2f, 1.f});
        mTranscoder.wait();
    }
    if (mTcRunner.isRunning()) {
        appendEngineLog("[GUI] Waiting for manual transcode to finish before cleanup...",
                        {1.f, 0.8f, 0.2f, 1.f});
        mTcRunner.wait();
    }
    if (mOrRunner.isRunning()) {
        appendEngineLog("[GUI] Waiting for offline render to finish before cleanup...",
                        {1.f, 0.8f, 0.2f, 1.f});
        mOrRunner.wait();
    }
    cleanupOwnedTempSessions(true);
}

void App::renderUI() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f);
    const float logoH = ImGui::GetTextLineHeight();
    if (mLogoTexId != 0) ImGui::Image((ImTextureID)(intptr_t)mLogoTexId, ImVec2(logoH, logoH));
    else ImGui::TextColored({0.60f, 0.57f, 0.52f, 1.f}, "⊙");
    ImGui::SameLine(0.f, 8.f);
    ImGui::Text("Spatial Root");
    ImGui::SameLine(0.f, 6.f);
    ImGui::TextDisabled("Real-Time Engine");

    const float leftEnd = ImGui::GetItemRectMax().x;
    const char* crumb = "ADM  →  LUSID  →  Spatial Render";
    const float crumbW = ImGui::CalcTextSize(crumb).x;
    const float crumbX = (ImGui::GetWindowWidth() - crumbW) * 0.5f;
    if (crumbX > leftEnd + 8.f) {
        ImGui::SameLine(crumbX);
        ImGui::TextDisabled("%s", crumb);
    }

    char stateBuf[64];
    snprintf(stateBuf, sizeof(stateBuf), "●  %s", stateName(mState));
    const float stateW = ImGui::CalcTextSize(stateBuf).x + 16.f;
    ImGui::SameLine(ImGui::GetWindowWidth() - stateW);
    ImGui::TextColored(stateColor(mState), "%s", stateBuf);
    ImGui::Separator();

    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("ENGINE")) {
            renderEngineTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("TRANSCODE")) {
            renderTranscodeTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("OFFLINE RENDER")) {
            renderOfflineRenderTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void App::renderEngineTab() {
    const bool isRunning = (mState == AppState::Running || mState == AppState::Paused);
    const bool isIdle = (mState == AppState::Idle || mState == AppState::Error);
    const ImVec4 kGreen = {0.20f, 0.62f, 0.25f, 1.f};
    const ImVec4 kAmber = {0.70f, 0.45f, 0.08f, 1.f};
    const ImVec4 kRed = {0.72f, 0.18f, 0.15f, 1.f};

    if (ImGui::BeginChild("##inputcard", {0.f, 260.f}, true)) {
        ImGui::TextDisabled("INPUT CONFIGURATION");
        ImGui::Spacing();
        if (isRunning) ImGui::BeginDisabled(true);

        ImGui::TextDisabled("SOURCE");
        if (mSourceIsAdm) { ImGui::SameLine(); ImGui::TextColored(kGreen, "ADM"); }
        else if (mSourceIsLusid) { ImGui::SameLine(); ImGui::TextColored(kGreen, "LUSID"); }
        ImGui::SameLine(120.f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
        if (ImGui::InputText("##source", &mSourcePath)) detectSource();
        ImGui::SameLine();
        if (ImGui::Button("Browse##src")) {
            const std::string p = pickFileOrDirectory("Select Audio Source");
            if (!p.empty()) { mSourcePath = p; detectSource(); }
        }
        if (!mSourceHint.empty()) {
            const bool isError = !mSourceIsAdm && !mSourceIsLusid;
            ImGui::SetCursorPosX(120.f);
            ImGui::TextColored(isError ? kRed : kGreen, "%s", mSourceHint.c_str());
        }
        ImGui::SetCursorPosX(120.f);
        if (ImGui::Button("Download Atmos Examples##atmosdl")) {
#ifdef __APPLE__
            system("open https://huggingface.co/datasets/lucianparisi/atmos-data/tree/main");
#elif defined(_WIN32)
            system("start https://huggingface.co/datasets/lucianparisi/atmos-data/tree/main");
#else
            system("xdg-open https://huggingface.co/datasets/lucianparisi/atmos-data/tree/main");
#endif
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Download Atmos example files");

        ImGui::TextDisabled("LAYOUT");
        ImGui::SameLine(120.f);
        ImGui::SetNextItemWidth(110.f);
        if (ImGui::Combo("##layoutpreset", &mLayoutPreset, kLayoutNames, IM_ARRAYSIZE(kLayoutNames))) {
            if (mLayoutPreset < IM_ARRAYSIZE(kLayoutNames) - 1) mLayoutPath = resolveProjectPath(kLayoutPaths[mLayoutPreset]);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
        ImGui::InputText("##layout", &mLayoutPath);
        ImGui::SameLine();
        if (ImGui::Button("Browse##layout")) {
            const std::string p = pickFile("Select Speaker Layout", {"*.json"}, "JSON files");
            if (!p.empty()) { mLayoutPath = p; mLayoutPreset = IM_ARRAYSIZE(kLayoutNames) - 1; }
        }

        renderDefaultLayoutControls();

        ImGui::SetCursorPosX(120.f);
        if (ImGui::Button("Layout Builder##layoutbuilder")) {
#ifdef __APPLE__
            system("open https://cultdsp.com/layout-builder/");
#elif defined(_WIN32)
            system("start https://cultdsp.com/layout-builder/");
#else
            system("xdg-open https://cultdsp.com/layout-builder/");
#endif
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Build or modify a custom layout here");

        ImGui::TextDisabled("DEVICE");
        ImGui::SameLine(120.f);
        if (ImGui::Button("Scan##device")) scanDevices();
        ImGui::SameLine();
        if (mDeviceList.empty()) {
            ImGui::TextDisabled("(click Scan to list output devices)");
        } else {
            std::vector<const char*> items;
            for (const auto& d : mDeviceList) items.push_back(d.c_str());
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 8.f);
            if (ImGui::Combo("##device", &mDeviceIdx, items.data(), (int)items.size())) {
                mDeviceName = (mDeviceIdx == 0) ? "" : mDeviceList[mDeviceIdx];
            }
        }

        ImGui::TextDisabled("BUFFER");
        ImGui::SameLine(120.f);
        ImGui::SetNextItemWidth(100.f);
        ImGui::Combo("##bufsize", &mBufferSizeIdx, kBufferSizeNames, 5);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Buffer size affects realtime stability.\n"
                "\n"
                "Smaller buffers reduce latency but increase CPU load and the risk of xruns, clicks, or dropouts.\n"
                "Larger buffers improve stability but increase latency.\n"
                "Changing buffer size may require restarting the engine or playback session.\n"
                "Safest workflow: stop playback before changing this setting."
            );
        }

        if (isRunning) ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::Spacing();

    if (ImGui::BeginChild("##transportcard", {0.f, 110.f}, true)) {
        ImGui::TextDisabled("TRANSPORT");
        ImGui::Spacing();

        const bool canStart = isIdle;
        const bool canStop = isRunning;
        const bool canPause = (mState == AppState::Running);
        const bool canResume = (mState == AppState::Paused);
        const bool busy = (mState == AppState::Transcoding);

        if (!canStart) ImGui::BeginDisabled(true);
        if (ImGui::Button("Start")) onStart();
        if (!canStart) ImGui::EndDisabled();
        ImGui::SameLine();
        if (!canStop) ImGui::BeginDisabled(true);
        if (ImGui::Button("Stop")) onStop();
        if (!canStop) ImGui::EndDisabled();
        ImGui::SameLine();
        if (!canPause) ImGui::BeginDisabled(true);
        if (ImGui::Button("Pause")) onPause();
        if (!canPause) ImGui::EndDisabled();
        ImGui::SameLine();
        if (!canResume) ImGui::BeginDisabled(true);
        if (ImGui::Button("Resume")) onResume();
        if (!canResume) ImGui::EndDisabled();

        ImGui::SameLine();
        if (!mLastGeneratedSceneAvailable || !mActiveTempSessionRoot) ImGui::BeginDisabled(true);
        if (ImGui::Button("Save Generated Scene")) {
            saveGeneratedSceneCopy(*mActiveTempSessionRoot, mActiveTempManifest,
                                   "Choose Folder for Generated Scene",
                                   "Generated scene saved");
        }
        if (!mLastGeneratedSceneAvailable || !mActiveTempSessionRoot) ImGui::EndDisabled();

        ImGui::SameLine();
        const bool hasDiagnostics = hasDiagnosticsFiles(mActiveTempSessionRoot) ||
                                    hasDiagnosticsFiles(mTcTempSessionRoot);
        if (!hasDiagnostics) ImGui::BeginDisabled(true);
        if (ImGui::Button("Save Diagnostic Files")) {
            if (mActiveTempSessionRoot) {
                saveDiagnosticsCopy(*mActiveTempSessionRoot, mActiveTempManifest,
                                    "Choose Folder for Diagnostic Files",
                                    "Diagnostic files saved");
            } else if (mTcTempSessionRoot) {
                saveDiagnosticsCopy(*mTcTempSessionRoot, mTcTempManifest,
                                    "Choose Folder for Diagnostic Files",
                                    "Diagnostic files saved");
            }
        }
        if (!hasDiagnostics) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Clear Temporary Files")) cleanupOwnedTempSessions(true);

        if (isRunning) {
            ImGui::Text("t=%.1fs", mStatus.timeSec);
            ImGui::SameLine();
            ImGui::Text("CPU=%.1f%%", mStatus.cpuLoad * 100.f);
            ImGui::SameLine();
            ImGui::Text("RMS=%.4f", mStatus.mainRms);
            ImGui::SameLine();
            ImGui::Text("Xrun=%zu", mStatus.xruns);
        } else if (busy) {
            ImGui::TextColored(kAmber, "Transcoding ADM into a temporary session...");
        } else if (mState == AppState::Error && !mLastError.empty()) {
            ImGui::TextColored(kRed, "Error: %s", mLastError.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::Spacing();

    if (ImGui::BeginChild("##ctrlcard", {0.f, 190.f}, true)) {
        ImGui::TextDisabled("RUNTIME CONTROLS");
        ImGui::Spacing();
        if (!isRunning) ImGui::BeginDisabled(true);

        ImGui::TextDisabled("MASTER GAIN");
        ImGui::SameLine(160.f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.f);
        if (ImGui::SliderFloat("##gain", &mGainDb, -60.f, 12.f, "%.1f dB") && isRunning) mSession->setMasterGainDb(mGainDb);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.f);
        if (ImGui::InputFloat("##gaininput", &mGainDb, 0.f, 0.f, "%.1f") && isRunning) {
            mGainDb = std::clamp(mGainDb, -60.f, 12.f);
            mSession->setMasterGainDb(mGainDb);
        }

        ImGui::TextDisabled("DBAP FOCUS");
        ImGui::SameLine(160.f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.f);
        if (ImGui::SliderFloat("##focus", &mFocus, 0.2f, 5.0f, "%.2f") && isRunning) mSession->setDbapFocus(mFocus);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.f);
        if (ImGui::InputFloat("##focusinput", &mFocus, 0.f, 0.f, "%.2f") && isRunning) {
            mFocus = std::clamp(mFocus, 0.2f, 5.0f);
            mSession->setDbapFocus(mFocus);
        }

        ImGui::TextDisabled("SPEAKER MIX (DB)");
        ImGui::SameLine(160.f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.f);
        if (ImGui::SliderFloat("##spkmix", &mSpkMixDb, -60.f, 12.f, "%.1f dB") && isRunning) mSession->setSpeakerMixDb(mSpkMixDb);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.f);
        if (ImGui::InputFloat("##spkmixinput", &mSpkMixDb, 0.f, 0.f, "%.1f") && isRunning) {
            mSpkMixDb = std::clamp(mSpkMixDb, -60.f, 12.f);
            mSession->setSpeakerMixDb(mSpkMixDb);
        }

        ImGui::TextDisabled("SUB MIX (DB)");
        ImGui::SameLine(160.f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.f);
        if (ImGui::SliderFloat("##submix", &mSubMixDb, -60.f, 12.f, "%.1f dB") && isRunning) mSession->setSubMixDb(mSubMixDb);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.f);
        if (ImGui::InputFloat("##submixinput", &mSubMixDb, 0.f, 0.f, "%.1f") && isRunning) {
            mSubMixDb = std::clamp(mSubMixDb, -60.f, 12.f);
            mSession->setSubMixDb(mSubMixDb);
        }

        ImGui::TextDisabled("ELEVATION MODE");
        ImGui::SameLine(160.f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 8.f);
        if (ImGui::Combo("##elevmode", &mElevationMode, kElevModeNames, 3) && isRunning) {
            mSession->setElevationMode(static_cast<ElevationMode>(mElevationMode));
        }

        if (!isRunning) ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::Spacing();

    const float logH = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginChild("##logcard", {0.f, logH}, true)) {
        ImGui::TextDisabled("ENGINE LOG");
        ImGui::Spacing();
        if (ImGui::BeginChild("##enginelog", {0.f, ImGui::GetContentRegionAvail().y}, false,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            for (const auto& entry : mEngineLog) ImGui::TextColored(entry.color, "%s", entry.text.c_str());
            if (mEngineLogAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.f) ImGui::SetScrollHereY(1.f);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

void App::renderTranscodeTab() {
    const ImVec4 kGreen = {0.20f, 0.62f, 0.25f, 1.f};
    const ImVec4 kAmber = {0.70f, 0.45f, 0.08f, 1.f};
    const ImVec4 kRed   = {0.72f, 0.18f, 0.15f, 1.f};
    const bool tcBusy = mTcRunner.isRunning();

    // ── Workflow selector tabs ────────────────────────────────────────────────
    // Each tab exposes one cult-transcoder subcommand.
    // The shared log and run-status section sit below the tab bar.

    std::string cmdPreview;  // built by whichever tab is active

    if (ImGui::BeginTabBar("##tc_workflow")) {

        // ── Tab 0: ADM to LUSID Scene (cult-transcoder transcode) ────────────
        if (ImGui::BeginTabItem(kTcWorkflowNames[0])) {
            mTcWorkflow = 0;
            if (ImGui::BeginChild("##tc0config", {0.f, 196.f}, true)) {
                ImGui::TextDisabled("ADM TO LUSID SCENE");
                ImGui::SameLine(); ImGui::TextDisabled("— convert ADM XML or ADM WAV/BWF metadata into a LUSID scene JSON");
                ImGui::Spacing();
                if (tcBusy) ImGui::BeginDisabled(true);

                ImGui::TextDisabled("INPUT");
                ImGui::SameLine(140.f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                ImGui::InputText("##tc0in", &mTcInput);
                ImGui::SameLine();
                if (ImGui::Button("Browse##tc0in")) {
                    const std::string p = pickFile("Select ADM Input", {"*.wav", "*.xml"}, "ADM files");
                    if (!p.empty()) mTcInput = p;
                }
                if (mTcInput.empty()) {
                    ImGui::SetCursorPosX(140.f);
                    ImGui::TextColored(kAmber, "Required: ADM WAV (.wav) or ADM XML (.xml)");
                }

                ImGui::TextDisabled("FORMAT");
                ImGui::SameLine(140.f);
                ImGui::SetNextItemWidth(160.f);
                ImGui::Combo("##tc0fmt", &mTcInFormat, kTcFormatNames, 3);
                ImGui::SameLine(); ImGui::TextDisabled("(Auto-detect uses file extension)");

                ImGui::TextDisabled("LFE MODE");
                ImGui::SameLine(140.f);
                ImGui::SetNextItemWidth(180.f);
                ImGui::Combo("##tc0lfe", &mTcLfeMode, kTcLfeModeNames, 2);

                ImGui::TextDisabled("OUTPUT");
                ImGui::SameLine(140.f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 8.f);
                ImGui::InputText("##tc0out", &mTcOutput);
                ImGui::SameLine(140.f);
                ImGui::TextDisabled("scene.lusid.json path — empty = temp session");

                ImGui::TextDisabled("TEMP FILES");
                ImGui::SameLine(140.f);
                ImGui::Checkbox("Keep temp files for debugging##tc0", &mKeepTempSessions);

                if (tcBusy) ImGui::EndDisabled();
            }
            ImGui::EndChild();

            // Build command preview for workflow 0
            {
                std::string fmt = kTcFormatValues[mTcInFormat];
                if (mTcInFormat == 0) {
                    std::string lower = mTcInput;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    fmt = (lower.size() > 4 && lower.substr(lower.size() - 4) == ".xml") ? "adm_xml" : "adm_wav";
                }
                const std::string outDisp = mTcOutput.empty() ? "<temp>/scene.lusid.json" : mTcOutput;
                cmdPreview = "cult-transcoder transcode"
                    " --in " + (mTcInput.empty() ? "<input>" : mTcInput) +
                    " --in-format " + fmt +
                    " --out " + outDisp +
                    " --out-format lusid_json"
                    " --lfe-mode " + kTcLfeModeValues[mTcLfeMode];
            }

            // ── Run controls for workflow 0 ──────────────────────────────────
            if (ImGui::BeginChild("##tc0ctrl", {0.f, 58.f}, true)) {
                if (tcBusy) ImGui::BeginDisabled(true);
                if (ImGui::Button("Run — ADM to LUSID Scene", {220.f, 0.f})) {
                    const std::string cultBin = findCultTranscoder();
                    if (cultBin.empty()) {
                        appendTcLog("[error] cult-transcoder not found. Build with ./build.sh --cult-only");
                    } else if (mTcInput.empty()) {
                        appendTcLog("[error] Input path is required.");
                    } else {
                        std::string fmt = kTcFormatValues[mTcInFormat];
                        if (mTcInFormat == 0) {
                            std::string lower = mTcInput;
                            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                            fmt = (lower.size() > 4 && lower.substr(lower.size() - 4) == ".xml") ? "adm_xml" : "adm_wav";
                        }
                        clearStandaloneTranscodeTempState();
                        mTcDone = false; mTcSuccess = false; mTcRunning = true;

                        fs::path outputPath;
                        fs::path reportPath;
                        if (!mTcOutput.empty()) {
                            outputPath = fs::path(mTcOutput);
                            reportPath = outputPath.parent_path() / (outputPath.stem().string() + "_report.json");
                        } else {
                            mTcTempSessionRoot = createOwnedTempSession("manual_transcode", mTcInput, mTcTempManifest);
                            outputPath = *mTcTempSessionRoot / "scene.lusid.json";
                            reportPath = *mTcTempSessionRoot / "reports" / "transcode_report.json";
                        }
                        try { fs::create_directories(outputPath.parent_path()); } catch (...) {}
                        try { fs::create_directories(reportPath.parent_path()); } catch (...) {}

                        std::vector<std::string> args = {
                            cultBin, "transcode",
                            "--in", mTcInput, "--in-format", fmt,
                            "--out", outputPath.string(), "--out-format", "lusid_json",
                            "--report", reportPath.string(),
                            "--stdout-report",
                            "--lfe-mode", kTcLfeModeValues[mTcLfeMode],
                        };
                        appendTcLog("[GUI] Running: cult-transcoder transcode ...");
                        mTcRunner.start(args, [this](const std::string& line) { appendTcLog(line); });
                    }
                }
                if (tcBusy) ImGui::EndDisabled();

                const char* st = tcBusy ? "Running..." : mTcDone ? (mTcSuccess ? "Complete" : "Failed") : "Idle";
                const ImVec4 sc = tcBusy ? kAmber : mTcDone ? (mTcSuccess ? kGreen : kRed)
                                                             : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                const float sw = ImGui::CalcTextSize(st).x + 20.f;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - sw);
                ImGui::TextColored(sc, "●  %s", st);
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        // ── Tab 1: ADM WAV to LUSID Package (cult-transcoder package-adm-wav) ─
        if (ImGui::BeginTabItem(kTcWorkflowNames[1])) {
            mTcWorkflow = 1;
            if (ImGui::BeginChild("##tc1config", {0.f, 196.f}, true)) {
                ImGui::TextDisabled("ADM WAV TO LUSID PACKAGE");
                ImGui::SameLine(); ImGui::TextDisabled("— extract ADM XML, convert metadata, split audio into a LUSID package");
                ImGui::Spacing();
                if (tcBusy) ImGui::BeginDisabled(true);

                ImGui::TextDisabled("INPUT WAV");
                ImGui::SameLine(160.f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                ImGui::InputText("##tc1in", &mTcPkgInput);
                ImGui::SameLine();
                if (ImGui::Button("Browse##tc1in")) {
                    const std::string p = pickFile("Select ADM WAV/BWF", {"*.wav"}, "WAV files");
                    if (!p.empty()) mTcPkgInput = p;
                }
                if (mTcPkgInput.empty()) {
                    ImGui::SetCursorPosX(160.f);
                    ImGui::TextColored(kAmber, "Required: ADM BWF/WAV source file (.wav)");
                }

                ImGui::TextDisabled("OUTPUT PACKAGE");
                ImGui::SameLine(160.f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                ImGui::InputText("##tc1out", &mTcPkgOutput);
                ImGui::SameLine();
                if (ImGui::Button("Browse##tc1out")) {
                    const std::string p = pickDirectory("Select Output Package Directory");
                    if (!p.empty()) mTcPkgOutput = p;
                }
                if (mTcPkgOutput.empty()) {
                    ImGui::SetCursorPosX(160.f);
                    ImGui::TextColored(kAmber, "Required: output directory for LUSID package");
                }

                ImGui::TextDisabled("LFE MODE");
                ImGui::SameLine(160.f);
                ImGui::SetNextItemWidth(180.f);
                ImGui::Combo("##tc1lfe", &mTcPkgLfeMode, kTcLfeModeNames, 2);

                ImGui::Spacing();
                ImGui::SetCursorPosX(160.f);
                ImGui::TextDisabled("Output: scene.lusid.json + mono stems + channel_order.txt + scene_report.json");

                if (tcBusy) ImGui::EndDisabled();
            }
            ImGui::EndChild();

            // Build command preview for workflow 1
            cmdPreview = "cult-transcoder package-adm-wav"
                " --in " + (mTcPkgInput.empty() ? "<source.wav>" : mTcPkgInput) +
                " --out-package " + (mTcPkgOutput.empty() ? "<package-dir>" : mTcPkgOutput) +
                " --lfe-mode " + kTcLfeModeValues[mTcPkgLfeMode] +
                " --stdout-report";

            // ── Run controls for workflow 1 ──────────────────────────────────
            if (ImGui::BeginChild("##tc1ctrl", {0.f, 58.f}, true)) {
                if (tcBusy) ImGui::BeginDisabled(true);
                const bool canRun1 = !mTcPkgInput.empty() && !mTcPkgOutput.empty();
                if (!canRun1) ImGui::BeginDisabled(true);
                if (ImGui::Button("Run — ADM WAV to LUSID Package", {260.f, 0.f})) {
                    const std::string cultBin = findCultTranscoder();
                    if (cultBin.empty()) {
                        appendTcLog("[error] cult-transcoder not found. Build with ./build.sh --cult-only");
                    } else {
                        clearStandaloneTranscodeTempState();
                        mTcDone = false; mTcSuccess = false; mTcRunning = true;

                        try { fs::create_directories(fs::path(mTcPkgOutput)); } catch (...) {}

                        const fs::path reportPath = fs::path(mTcPkgOutput) / "scene_report.json";
                        std::vector<std::string> args = {
                            cultBin, "package-adm-wav",
                            "--in", mTcPkgInput,
                            "--out-package", mTcPkgOutput,
                            "--report", reportPath.string(),
                            "--stdout-report",
                            "--lfe-mode", kTcLfeModeValues[mTcPkgLfeMode],
                        };
                        appendTcLog("[GUI] Running: cult-transcoder package-adm-wav ...");
                        mTcRunner.start(args, [this](const std::string& line) { appendTcLog(line); });
                    }
                }
                if (!canRun1) ImGui::EndDisabled();
                if (tcBusy) ImGui::EndDisabled();

                const char* st = tcBusy ? "Running..." : mTcDone ? (mTcSuccess ? "Complete" : "Failed") : "Idle";
                const ImVec4 sc = tcBusy ? kAmber : mTcDone ? (mTcSuccess ? kGreen : kRed)
                                                             : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                const float sw = ImGui::CalcTextSize(st).x + 20.f;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - sw);
                ImGui::TextColored(sc, "●  %s", st);
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        // ── Tab 2: LUSID to ADM Export (cult-transcoder adm-author) ──────────
        if (ImGui::BeginTabItem(kTcWorkflowNames[2])) {
            mTcWorkflow = 2;
            if (ImGui::BeginChild("##tc2config", {0.f, 260.f}, true)) {
                ImGui::TextDisabled("LUSID TO ADM EXPORT");
                ImGui::SameLine(); ImGui::TextDisabled("— author LUSID package material into Logic-compatible ADM BWF/WAV");
                ImGui::Spacing();
                if (tcBusy) ImGui::BeginDisabled(true);

                ImGui::TextDisabled("INPUT MODE");
                ImGui::SameLine(160.f);
                ImGui::SetNextItemWidth(240.f);
                ImGui::Combo("##tc2mode", &mTcAdmInputMode, kTcAdmInputModeNames, 2);

                if (mTcAdmInputMode == 0) {
                    // Mode 0: explicit scene.lusid.json + wav-dir
                    ImGui::TextDisabled("SCENE JSON");
                    ImGui::SameLine(160.f);
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                    ImGui::InputText("##tc2lusid", &mTcAdmLusid);
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##tc2lusid")) {
                        const std::string p = pickFile("Select scene.lusid.json", {"*.json"}, "JSON files");
                        if (!p.empty()) mTcAdmLusid = p;
                    }
                    if (mTcAdmLusid.empty()) {
                        ImGui::SetCursorPosX(160.f); ImGui::TextColored(kAmber, "Required: scene.lusid.json");
                    }

                    ImGui::TextDisabled("WAV DIRECTORY");
                    ImGui::SameLine(160.f);
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                    ImGui::InputText("##tc2wavdir", &mTcAdmWavDir);
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##tc2wavdir")) {
                        const std::string p = pickDirectory("Select Stem WAV Directory");
                        if (!p.empty()) mTcAdmWavDir = p;
                    }
                    if (mTcAdmWavDir.empty()) {
                        ImGui::SetCursorPosX(160.f); ImGui::TextColored(kAmber, "Required: directory containing mono stem WAVs");
                    }
                } else {
                    // Mode 1: LUSID package directory
                    ImGui::TextDisabled("LUSID PACKAGE");
                    ImGui::SameLine(160.f);
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                    ImGui::InputText("##tc2pkg", &mTcAdmLusidPkg);
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##tc2pkg")) {
                        const std::string p = pickDirectory("Select LUSID Package Directory");
                        if (!p.empty()) mTcAdmLusidPkg = p;
                    }
                    if (mTcAdmLusidPkg.empty()) {
                        ImGui::SetCursorPosX(160.f); ImGui::TextColored(kAmber, "Required: LUSID package directory");
                    }
                }

                ImGui::TextDisabled("OUTPUT XML");
                ImGui::SameLine(160.f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                ImGui::InputText("##tc2outxml", &mTcAdmOutXml);
                ImGui::SameLine();
                if (ImGui::Button("Browse##tc2outxml")) {
                    const std::string p = pickDirectory("Select Output Directory for ADM XML");
                    if (!p.empty()) mTcAdmOutXml = (fs::path(p) / "export.adm.xml").string();
                }
                if (mTcAdmOutXml.empty()) {
                    ImGui::SetCursorPosX(160.f); ImGui::TextColored(kAmber, "Required: output .adm.xml path");
                }

                ImGui::TextDisabled("OUTPUT WAV");
                ImGui::SameLine(160.f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                ImGui::InputText("##tc2outwav", &mTcAdmOutWav);
                ImGui::SameLine();
                if (ImGui::Button("Browse##tc2outwav")) {
                    const std::string p = pickDirectory("Select Output Directory for ADM WAV");
                    if (!p.empty()) mTcAdmOutWav = (fs::path(p) / "export.wav").string();
                }
                if (mTcAdmOutWav.empty()) {
                    ImGui::SetCursorPosX(160.f); ImGui::TextColored(kAmber, "Required: output ADM BWF/WAV path");
                }

                // ── Advanced / experimental options ──────────────────────────
                ImGui::Spacing();
                if (ImGui::TreeNode("Advanced / Experimental Options")) {
                    ImGui::TextColored(kAmber, "These options are for compatibility testing only — not normal use.");
                    ImGui::Spacing();

                    ImGui::TextDisabled("DBMD SOURCE");
                    ImGui::SameLine(160.f);
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                    ImGui::InputText("##tc2dbmd", &mTcAdmDbmdSrc);
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##tc2dbmd")) {
                        const std::string p = pickFile("Select DBMD Source", {"*.wav", "*.bin"}, "WAV/BIN files");
                        if (!p.empty()) mTcAdmDbmdSrc = p;
                    }
                    ImGui::SetCursorPosX(160.f);
                    ImGui::TextDisabled("Extracts/injects Dolby dbmd chunk (experimental)");

                    ImGui::TextDisabled("METADATA POST-DATA");
                    ImGui::SameLine(160.f);
                    ImGui::Checkbox("Reorder WAV chunks for Dolby tool compatibility##tc2mpd", &mTcAdmMetadataPostData);
                    ImGui::SetCursorPosX(160.f);
                    ImGui::TextDisabled("Writes JUNK/fmt/data/axml/chna order (experimental)");

                    ImGui::TreePop();
                }

                if (tcBusy) ImGui::EndDisabled();
            }
            ImGui::EndChild();

            // Build command preview for workflow 2
            {
                cmdPreview = "cult-transcoder adm-author";
                if (mTcAdmInputMode == 0) {
                    cmdPreview += " --lusid " + (mTcAdmLusid.empty() ? "<scene.lusid.json>" : mTcAdmLusid);
                    cmdPreview += " --wav-dir " + (mTcAdmWavDir.empty() ? "<wav-dir>" : mTcAdmWavDir);
                } else {
                    cmdPreview += " --lusid-package " + (mTcAdmLusidPkg.empty() ? "<package-dir>" : mTcAdmLusidPkg);
                }
                cmdPreview += " --out-xml " + (mTcAdmOutXml.empty() ? "<export.adm.xml>" : mTcAdmOutXml);
                cmdPreview += " --out-wav " + (mTcAdmOutWav.empty() ? "<export.wav>" : mTcAdmOutWav);
                cmdPreview += " --stdout-report";
                if (!mTcAdmDbmdSrc.empty()) cmdPreview += " --dbmd-source " + mTcAdmDbmdSrc;
                if (mTcAdmMetadataPostData) cmdPreview += " --metadata-post-data";
            }

            // ── Run controls for workflow 2 ──────────────────────────────────
            if (ImGui::BeginChild("##tc2ctrl", {0.f, 58.f}, true)) {
                if (tcBusy) ImGui::BeginDisabled(true);
                const bool canRun2 = !mTcAdmOutXml.empty() && !mTcAdmOutWav.empty() &&
                    (mTcAdmInputMode == 0 ? (!mTcAdmLusid.empty() && !mTcAdmWavDir.empty())
                                          : !mTcAdmLusidPkg.empty());
                if (!canRun2) ImGui::BeginDisabled(true);
                if (ImGui::Button("Run — LUSID to ADM Export", {220.f, 0.f})) {
                    const std::string cultBin = findCultTranscoder();
                    if (cultBin.empty()) {
                        appendTcLog("[error] cult-transcoder not found. Build with ./build.sh --cult-only");
                    } else {
                        clearStandaloneTranscodeTempState();
                        mTcDone = false; mTcSuccess = false; mTcRunning = true;

                        try { fs::create_directories(fs::path(mTcAdmOutXml).parent_path()); } catch (...) {}
                        try { fs::create_directories(fs::path(mTcAdmOutWav).parent_path()); } catch (...) {}

                        const fs::path reportPath = fs::path(mTcAdmOutWav).parent_path() /
                            (fs::path(mTcAdmOutWav).stem().string() + "_report.json");
                        std::vector<std::string> args;
                        args.push_back(cultBin);
                        args.push_back("adm-author");
                        if (mTcAdmInputMode == 0) {
                            args.insert(args.end(), {"--lusid", mTcAdmLusid, "--wav-dir", mTcAdmWavDir});
                        } else {
                            args.insert(args.end(), {"--lusid-package", mTcAdmLusidPkg});
                        }
                        args.insert(args.end(), {
                            "--out-xml", mTcAdmOutXml,
                            "--out-wav", mTcAdmOutWav,
                            "--report", reportPath.string(),
                            "--stdout-report",
                        });
                        if (!mTcAdmDbmdSrc.empty()) { args.push_back("--dbmd-source"); args.push_back(mTcAdmDbmdSrc); }
                        if (mTcAdmMetadataPostData) args.push_back("--metadata-post-data");

                        appendTcLog("[GUI] Running: cult-transcoder adm-author ...");
                        mTcRunner.start(args, [this](const std::string& line) { appendTcLog(line); });
                    }
                }
                if (!canRun2) ImGui::EndDisabled();
                if (tcBusy) ImGui::EndDisabled();

                const char* st = tcBusy ? "Running..." : mTcDone ? (mTcSuccess ? "Complete" : "Failed") : "Idle";
                const ImVec4 sc = tcBusy ? kAmber : mTcDone ? (mTcSuccess ? kGreen : kRed)
                                                             : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                const float sw = ImGui::CalcTextSize(st).x + 20.f;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - sw);
                ImGui::TextColored(sc, "●  %s", st);
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // ── Command preview (shared, updated by whichever tab is active) ─────────
    ImGui::Spacing();
    ImGui::TextDisabled("CMD");
    ImGui::SameLine(60.f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.08f, 0.08f, 0.08f, 1.f});
    ImGui::InputText("##cmdpreview", &cmdPreview, ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // ── Shared transcode log ──────────────────────────────────────────────────
    const float logH = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginChild("##tclogcard", {0.f, logH}, true)) {
        ImGui::TextDisabled("TRANSCODE LOG");
        ImGui::Spacing();
        std::deque<LogEntry> tcLogSnapshot;
        {
            std::lock_guard<std::mutex> lock(mTcLogMutex);
            tcLogSnapshot = mTcLog;
        }
        if (ImGui::BeginChild("##tclog", {0.f, ImGui::GetContentRegionAvail().y}, false,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            for (const auto& entry : tcLogSnapshot) ImGui::TextColored(entry.color, "%s", entry.text.c_str());
            if (mTcLogAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.f) ImGui::SetScrollHereY(1.f);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

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

    resetRuntimeToDefaults();
    mLastError.clear();
    mLastFailureHasDiagnostics = false;

    if (mSourceIsAdm) {
        const std::string cultBin = findCultTranscoder();
        if (cultBin.empty()) {
            mLastError = "cult-transcoder binary not found. Build with ./build.sh first.";
            mState = AppState::Error;
            appendEngineLog("[GUI] " + mLastError, {1.f, 0.4f, 0.4f, 1.f});
            return;
        }

        clearTempSessionState();
        mTranscodeAdm = mSourcePath;
        mActiveTempSessionRoot = createOwnedTempSession("generated_scene", mSourcePath, mActiveTempManifest);
        mTranscodeScene = pathString(*mActiveTempSessionRoot / "scene.lusid.json");
        const fs::path reportPath = *mActiveTempSessionRoot / "reports" / "transcode_report.json";
        try { fs::create_directories(reportPath.parent_path()); } catch (...) {}

        std::vector<std::string> args = {
            cultBin, "transcode", "--in", mTranscodeAdm, "--in-format", "adm_wav",
            "--out", mTranscodeScene, "--out-format", "lusid_json",
            "--report", reportPath.string(), "--lfe-mode", "hardcoded",
        };
        appendEngineLog("[GUI] ADM source detected. Running cult-transcoder...",
                        {1.f, 0.8f, 0.2f, 1.f});
        mState = AppState::Transcoding;
        mTranscoder.start(args, [this](const std::string& line) { appendEngineLog("[transcoder] " + line); });
    } else {
        doLaunchEngine((fs::path(mSourcePath) / "scene.lusid.json").string(), mSourcePath, "");
    }
}

void App::onStop() {
    if (mState == AppState::Running || mState == AppState::Paused) {
        appendEngineLog("[GUI] Stopping engine...");
        mSession->shutdown();
        if (mActiveTempSessionRoot) updateManifest(*mActiveTempSessionRoot, mActiveTempManifest, "stopped", false, false);
        mState = AppState::Idle;
        appendEngineLog("[GUI] Engine stopped.");
    }
}

void App::onPause() {
    if (mState == AppState::Running) {
        mSession->setPaused(true);
        mState = AppState::Paused;
        appendEngineLog("[GUI] Paused.");
    }
}

void App::onResume() {
    if (mState == AppState::Paused) {
        mSession->setPaused(false);
        mState = AppState::Running;
        appendEngineLog("[GUI] Resumed.");
    }
}

void App::doLaunchEngine(const std::string& scenePath,
                         const std::string& sourcesFolder,
                         const std::string& admFile) {
    mSession = std::make_unique<EngineSession>();
    appendEngineLog("[GUI] Configuring engine...");

    EngineOptions opts;
    opts.sampleRate = 48000;
    opts.bufferSize = kBufferSizes[mBufferSizeIdx];
    opts.outputDeviceName = mDeviceName;
    opts.oscPort = 9009;
    opts.elevationMode = ElevationMode::RescaleAtmosUp;

    if (!mSession->configureEngine(opts)) {
        mLastError = mSession->getLastError();
        mState = AppState::Error;
        appendEngineLog("[Engine] configureEngine failed: " + mLastError, {1.f, 0.4f, 0.4f, 1.f});
        appendFailureDiagnostics(mSession->getFailureDiagnostics());
        return;
    }

    SceneInput scene;
    scene.scenePath = scenePath;
    scene.sourcesFolder = sourcesFolder;
    scene.admFile = admFile;
    if (!mSession->loadScene(scene)) {
        mLastError = mSession->getLastError();
        mState = AppState::Error;
        mLastFailureHasDiagnostics = true;
        if (mActiveTempSessionRoot) updateManifest(*mActiveTempSessionRoot, mActiveTempManifest, "failed", false, mKeepTempSessions);
        appendEngineLog("[Engine] loadScene failed: " + mLastError, {1.f, 0.4f, 0.4f, 1.f});
        appendFailureDiagnostics(mSession->getFailureDiagnostics());
        mSession->shutdown();
        return;
    }

    LayoutInput layout;
    layout.layoutPath = mLayoutPath;
    layout.remapCsvPath = mRemapPath;
    if (!mSession->applyLayout(layout)) {
        mLastError = mSession->getLastError();
        mState = AppState::Error;
        mLastFailureHasDiagnostics = true;
        if (mActiveTempSessionRoot) updateManifest(*mActiveTempSessionRoot, mActiveTempManifest, "failed", false, mKeepTempSessions);
        appendEngineLog("[Engine] applyLayout failed: " + mLastError, {1.f, 0.4f, 0.4f, 1.f});
        appendFailureDiagnostics(mSession->getFailureDiagnostics());
        mSession->shutdown();
        return;
    }

    RuntimeParams rp;
    rp.masterGainDb = mGainDb;
    rp.dbapFocus = mFocus;
    rp.speakerMixDb = mSpkMixDb;
    rp.subMixDb = mSubMixDb;
    if (!mSession->configureRuntime(rp)) {
        mLastError = mSession->getLastError();
        mState = AppState::Error;
        appendEngineLog("[Engine] configureRuntime failed: " + mLastError, {1.f, 0.4f, 0.4f, 1.f});
        appendFailureDiagnostics(mSession->getFailureDiagnostics());
        mSession->shutdown();
        return;
    }

    if (!mSession->start()) {
        mLastError = mSession->getLastError();
        mState = AppState::Error;
        appendEngineLog("[Engine] start() failed: " + mLastError, {1.f, 0.4f, 0.4f, 1.f});
        appendFailureDiagnostics(mSession->getFailureDiagnostics());
        mSession->shutdown();
        return;
    }

    mSession->setElevationMode(static_cast<ElevationMode>(mElevationMode));
    mState = AppState::Running;
    if (mActiveTempSessionRoot) {
        updateManifest(*mActiveTempSessionRoot, mActiveTempManifest, "running", false, false);
        mLastGeneratedSceneAvailable = true;
    }
    appendEngineLog("[Engine] Started successfully. OSC port 9009.", {0.3f, 0.9f, 0.3f, 1.f});
}

void App::tryLoadDefaultLayoutOnStartup() {
    const DefaultLayoutResult r = mDefaultLayoutMgr.loadDefaultLayout();
    mDefaultLayoutStatus     = r.status;
    mDefaultLayoutSavedAt    = r.savedAt;
    mDefaultLayoutName       = r.layoutName;
    mDefaultLayoutSourcePath = r.sourcePath;

    if (r.status == DefaultLayoutStatus::None) {
        appendEngineLog("[GUI] No default layout saved.");
        return;
    }
    if (!r.success) {
        appendEngineLog("[GUI] WARNING: Saved default layout could not be loaded: " + r.message,
                        {1.f, 0.8f, 0.2f, 1.f});
        return;
    }
    // Write the validated JSON to a temp file so we can point mLayoutPath at it.
    // We write it to the same settings dir — it IS the settings dir copy.
    mLayoutPath   = pathString(mDefaultLayoutMgr.layoutPath());
    mLayoutPreset = IM_ARRAYSIZE(kLayoutNames) - 1;  // "Custom"

    const std::string displayName = r.layoutName.empty() ? "default layout" : r.layoutName;
    appendEngineLog("[GUI] Default layout loaded: " + displayName, {0.3f, 0.9f, 0.3f, 1.f});
}

void App::onSetAsDefaultLayout() {
    if (mLayoutPath.empty()) {
        appendEngineLog("[GUI] No layout selected — cannot save as default.", {1.f, 0.5f, 0.2f, 1.f});
        return;
    }
    std::ifstream f(mLayoutPath);
    if (!f.is_open()) {
        appendEngineLog("[GUI] Cannot read layout file: " + mLayoutPath, {1.f, 0.4f, 0.4f, 1.f});
        return;
    }
    const std::string jsonText((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());

    const DefaultLayoutResult r = mDefaultLayoutMgr.saveDefaultLayout(jsonText, mLayoutPath);
    mDefaultLayoutStatus     = r.status;
    mDefaultLayoutSavedAt    = r.savedAt;
    mDefaultLayoutName       = r.layoutName;
    mDefaultLayoutSourcePath = r.sourcePath;

    if (r.success) {
        appendEngineLog("[GUI] Default layout saved: " +
                        (r.layoutName.empty() ? mLayoutPath : r.layoutName),
                        {0.3f, 0.9f, 0.3f, 1.f});
    } else {
        appendEngineLog("[GUI] Failed to save default layout: " + r.message, {1.f, 0.4f, 0.4f, 1.f});
    }
}

void App::onClearDefaultLayout() {
    const DefaultLayoutResult r = mDefaultLayoutMgr.clearDefaultLayout();
    mDefaultLayoutStatus     = DefaultLayoutStatus::None;
    mDefaultLayoutSavedAt.clear();
    mDefaultLayoutName.clear();
    mDefaultLayoutSourcePath.clear();
    appendEngineLog("[GUI] Default layout cleared.", {0.65f, 0.65f, 0.9f, 1.f});
    (void)r;
}

void App::renderDefaultLayoutControls() {
    const ImVec4 kGreen = {0.20f, 0.62f, 0.25f, 1.f};
    const ImVec4 kAmber = {0.70f, 0.45f, 0.08f, 1.f};
    const ImVec4 kRed   = {0.72f, 0.18f, 0.15f, 1.f};
    const ImVec4 kBlue  = {0.40f, 0.60f, 0.90f, 1.f};

    ImGui::TextDisabled("DEFAULT LAYOUT");
    ImGui::SameLine(160.f);

    // Status indicator
    switch (mDefaultLayoutStatus) {
        case DefaultLayoutStatus::None:
            ImGui::TextDisabled("none saved");
            break;
        case DefaultLayoutStatus::Loaded:
            ImGui::TextColored(kGreen, "loaded");
            if (!mDefaultLayoutName.empty()) {
                ImGui::SameLine(); ImGui::TextDisabled("(%s)", mDefaultLayoutName.c_str());
            }
            if (!mDefaultLayoutSavedAt.empty()) {
                ImGui::SameLine(); ImGui::TextDisabled("saved %s", mDefaultLayoutSavedAt.c_str());
            }
            break;
        case DefaultLayoutStatus::Invalid:
            ImGui::TextColored(kAmber, "saved file invalid — check layout JSON");
            break;
        case DefaultLayoutStatus::Unavailable:
            ImGui::TextColored(kRed, "unavailable (permission/read error)");
            break;
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 300.f);

    const bool hasLayout = !mLayoutPath.empty();
    if (!hasLayout) ImGui::BeginDisabled(true);
    if (ImGui::SmallButton("Set as Default")) onSetAsDefaultLayout();
    if (!hasLayout) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save current layout as startup default");

    ImGui::SameLine();
    const bool hasDefault = (mDefaultLayoutStatus != DefaultLayoutStatus::None);
    if (!hasDefault) ImGui::BeginDisabled(true);
    if (ImGui::SmallButton("Clear Default")) onClearDefaultLayout();
    if (!hasDefault) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove the saved default layout");
}

void App::resetRuntimeToDefaults() {
    mGainDb = 0.0f;
    mFocus = 1.5f;
    mSpkMixDb = 0.0f;
    mSubMixDb = 0.0f;
    mElevationMode = 0;
}

void App::scanDevices() {
    mDeviceList.clear();
    mDeviceList.push_back("(system default)");
    const int n = al::AudioDevice::numDevices();
    for (int i = 0; i < n; ++i) {
        al::AudioDevice dev(i);
        if (dev.valid() && dev.hasOutput()) mDeviceList.push_back(std::string(dev.name()));
    }
    mDeviceIdx = 0;
    mDeviceName = "";
}

void App::detectSource() {
    mSourceIsAdm = false;
    mSourceIsLusid = false;
    mSourceHint.clear();
    if (mSourcePath.empty()) return;

    std::error_code ec;
    const fs::path p(mSourcePath);
    if (!fs::exists(p, ec)) {
        mSourceHint = "Path does not exist";
        return;
    }
    if (fs::is_regular_file(p, ec)) {
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".wav") {
            mSourceIsAdm = true;
            mSourceHint = "Detected: ADM WAV";
        } else {
            mSourceHint = "Unrecognized file type — expected .wav (ADM)";
        }
    } else if (fs::is_directory(p, ec)) {
        if (fs::exists(p / "scene.lusid.json", ec)) {
            mSourceIsLusid = true;
            mSourceHint = "Detected: LUSID package";
        } else {
            mSourceHint = "Directory has no scene.lusid.json — not a LUSID package";
        }
    }
}

std::string App::resolveProjectPath(const std::string& relPath) const {
    if (relPath.empty()) return "";
    return (fs::path(mProjectRoot) / relPath).string();
}

std::string App::findCultTranscoder() const {
    std::vector<std::string> candidates = {
        resolveProjectPath("build/internal/cult_transcoder/cult-transcoder"),
        resolveProjectPath("internal/cult_transcoder/build/cult-transcoder"),
    };
#ifdef _WIN32
    for (auto& c : candidates) c += ".exe";
#endif
    for (const auto& c : candidates) {
        if (fs::exists(c)) return c;
    }
    return "";
}

std::string App::findSpatialRenderer() const {
    std::string candidate = resolveProjectPath(
        "build/source/spatial_engine/spatialRender/spatialroot_spatial_render");
#ifdef _WIN32
    candidate += ".exe";
#endif
    if (fs::exists(candidate)) return candidate;
    return "";
}

std::string App::transcodeOutputPath(const std::string& admPath) const {
    const fs::path p(admPath);
    return resolveProjectPath("data/processedData/stageForRender/" + p.stem().string() + ".lusid.json");
}

fs::path App::tempSessionsRoot() const {
    return SpatialRootPaths::tempSessionsRoot(mTempRootOverride);
}

fs::path App::createOwnedTempSession(const std::string& sessionType,
                                     const std::string& sourcePath,
                                     TempSessionManifest& manifestOut) {
    const fs::path sessionRoot = SpatialRootPaths::createTempSessionRoot(mTempRootOverride);
    manifestOut = {};
    manifestOut.sessionId = sessionRoot.filename().string();
    manifestOut.createdAtUtc = SpatialRootPaths::makeCreatedAtUtc();
    manifestOut.sourcePath = sourcePath;
    manifestOut.sessionType = sessionType;
    manifestOut.status = "created";
    manifestOut.saved = false;
    manifestOut.preserved = mKeepTempSessions;
    updateManifest(sessionRoot, manifestOut, "created", false, mKeepTempSessions);
    appendEngineLog("[GUI] Created temp session: " + pathString(sessionRoot), {0.65f, 0.65f, 0.9f, 1.f});
    return sessionRoot;
}

void App::updateManifest(const fs::path& sessionRoot, TempSessionManifest& manifest,
                         const std::string& status, bool saved, bool preserved) {
    manifest.status = status;
    manifest.saved = saved;
    manifest.preserved = preserved;
    if (manifest.createdAtUtc.empty()) manifest.createdAtUtc = SpatialRootPaths::makeCreatedAtUtc();
    SpatialRootPaths::writeManifest(sessionRoot, manifest);
}

bool App::saveGeneratedSceneCopy(const fs::path& sessionRoot, TempSessionManifest& manifest,
                                 const std::string& dialogTitle, const std::string& successLabel) {
    const std::string destination = pickDirectory(dialogTitle);
    if (destination.empty()) return false;
    try {
        const fs::path scenePath = sessionRoot / "scene.lusid.json";
        if (!copyIfPresent(scenePath, fs::path(destination) / "scene.lusid.json")) {
            throw std::runtime_error("No generated scene.lusid.json found in the temp session.");
        }
        updateManifest(sessionRoot, manifest, manifest.status, true, manifest.preserved);
        appendEngineLog("[GUI] " + successLabel + ": " + destination, {0.3f, 0.9f, 0.3f, 1.f});
        return true;
    } catch (const std::exception& e) {
        mLastError = e.what();
        mState = AppState::Error;
        appendEngineLog("[GUI] Save failed: " + mLastError, {1.f, 0.3f, 0.3f, 1.f});
        return false;
    }
}

bool App::saveDiagnosticsCopy(const fs::path& sessionRoot, TempSessionManifest& manifest,
                              const std::string& dialogTitle, const std::string& successLabel) {
    const std::string destination = pickDirectory(dialogTitle);
    if (destination.empty()) return false;
    try {
        const fs::path destinationRoot(destination);
        bool copiedAnything = false;
        copiedAnything = copyIfPresent(sessionRoot / "manifest.json",
                                       destinationRoot / "manifest.json") || copiedAnything;
        copiedAnything = copyIfPresent(sessionRoot / "reports",
                                       destinationRoot / "reports") || copiedAnything;
        if (!copiedAnything) {
            throw std::runtime_error("No diagnostic files were found in the temp session.");
        }
        updateManifest(sessionRoot, manifest, manifest.status, true, manifest.preserved);
        appendEngineLog("[GUI] " + successLabel + ": " + destination, {0.3f, 0.9f, 0.3f, 1.f});
        return true;
    } catch (const std::exception& e) {
        mLastError = e.what();
        mState = AppState::Error;
        appendEngineLog("[GUI] Save failed: " + mLastError, {1.f, 0.3f, 0.3f, 1.f});
        return false;
    }
}

void App::cleanupOwnedTempSessions(bool forceNow) {
    auto cleanupOne = [&](std::optional<fs::path>& sessionRoot,
                          TempSessionManifest& manifest,
                          bool clearGenerated,
                          bool clearDiagnostics) {
        if (!sessionRoot) return;
        if (!forceNow && (mState == AppState::Running || mState == AppState::Paused || mState == AppState::Transcoding)) return;
        if (mKeepTempSessions || manifest.preserved) {
            updateManifest(*sessionRoot, manifest, manifest.status, manifest.saved, true);
            appendEngineLog("[GUI] Preserving temp session: " + pathString(*sessionRoot),
                            {1.f, 0.8f, 0.2f, 1.f});
            return;
        }
        if (SpatialRootPaths::deleteTempSession(*sessionRoot, tempSessionsRoot())) {
            appendEngineLog("[GUI] Deleted temp session: " + pathString(*sessionRoot),
                            {0.65f, 0.65f, 0.9f, 1.f});
            sessionRoot.reset();
            manifest = {};
            if (clearGenerated) mLastGeneratedSceneAvailable = false;
            if (clearDiagnostics) mLastFailureHasDiagnostics = false;
        }
    };

    cleanupOne(mActiveTempSessionRoot, mActiveTempManifest, true, true);
    cleanupOne(mTcTempSessionRoot, mTcTempManifest, false, true);
}

void App::clearTempSessionState() {
    mActiveTempSessionRoot.reset();
    mActiveTempManifest = {};
    mLastGeneratedSceneAvailable = false;
}

void App::clearStandaloneTranscodeTempState() {
    mTcTempSessionRoot.reset();
    mTcTempManifest = {};
}

std::string App::pathString(const fs::path& path) {
    return path.string();
}

void App::appendEngineLog(const std::string& text, ImVec4 color) {
    mEngineLog.push_back({color, text});
    if (mEngineLog.size() > 2000) mEngineLog.pop_front();
}

void App::appendFailureDiagnostics(const std::string& diagnostics) {
    if (diagnostics.empty()) return;
    // Split the diagnostic block (already formatted by EngineSession) by newline
    // and append each line as a separate log entry with a muted amber colour.
    const ImVec4 kDiagColor = {1.f, 0.6f, 0.2f, 1.f};
    const ImVec4 kBodyColor = {0.9f, 0.75f, 0.55f, 1.f};
    std::istringstream iss(diagnostics);
    std::string line;
    while (std::getline(iss, line)) {
        const bool isHeader = (line.find("===") != std::string::npos);
        appendEngineLog(line, isHeader ? kDiagColor : kBodyColor);
    }
}

void App::appendTcLog(const std::string& line) {
    ImVec4 color = {0.85f, 0.85f, 0.85f, 1.f};
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("error") != std::string::npos) color = {1.f, 0.35f, 0.35f, 1.f};
    else if (lower.find("warn") != std::string::npos) color = {1.f, 0.8f, 0.2f, 1.f};
    else if (line.size() > 3 && line.substr(0, 4) == "[ok]") color = {0.3f, 0.9f, 0.3f, 1.f};

    std::lock_guard<std::mutex> lock(mTcLogMutex);
    mTcLog.push_back({color, line});
    if (mTcLog.size() > 2000) mTcLog.pop_front();
}

void App::appendOrLog(const std::string& line) {
    ImVec4 color = {0.85f, 0.85f, 0.85f, 1.f};
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("error") != std::string::npos)      color = {1.f,  0.35f, 0.35f, 1.f};
    else if (lower.find("warn") != std::string::npos)  color = {1.f,  0.8f,  0.2f,  1.f};
    else if (lower.find("done") != std::string::npos)  color = {0.3f, 0.9f,  0.3f,  1.f};
    else if (lower.find("complete") != std::string::npos) color = {0.3f, 0.9f, 0.3f, 1.f};

    std::lock_guard<std::mutex> lock(mOrLogMutex);
    mOrLog.push_back({color, line});
    if (mOrLog.size() > 2000) mOrLog.pop_front();
}

void App::renderOfflineRenderTab() {
    const ImVec4 kGreen = {0.20f, 0.62f, 0.25f, 1.f};
    const ImVec4 kAmber = {0.70f, 0.45f, 0.08f, 1.f};
    const ImVec4 kRed   = {0.72f, 0.18f, 0.15f, 1.f};
    const bool orBusy = mOrRunner.isRunning();

    std::string cmdPreview;

    if (ImGui::BeginTabBar("##or_mode")) {

        // ── Tab 0: ADM WAV via CULT ──────────────────────────────────────────
        if (ImGui::BeginTabItem("ADM WAV Offline Render (Experimental)")) {
            if (ImGui::BeginChild("##or0config", {0.f, 220.f}, true)) {
                ImGui::TextDisabled("ADM WAV OFFLINE RENDER");
                ImGui::SameLine();
                ImGui::TextDisabled("— ADM WAV input → CULT → LUSID scene → offline renderer → speaker WAV");
                ImGui::Spacing();
                ImGui::TextColored(kAmber, "Experimental:");
                ImGui::SameLine();
                ImGui::TextDisabled("Uses CULT to extract ADM metadata, then renders through Spatial Root's offline renderer.");
                ImGui::Spacing();
                if (orBusy) ImGui::BeginDisabled(true);

                ImGui::TextDisabled("ADM WAV INPUT");
                ImGui::SameLine(160.f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                ImGui::InputText("##or0adm", &mOrAdmInput);
                ImGui::SameLine();
                if (ImGui::Button("Browse##or0adm")) {
                    const std::string p = pickFile("Select ADM WAV", {"*.wav"}, "WAV files");
                    if (!p.empty()) mOrAdmInput = p;
                }
                if (mOrAdmInput.empty()) {
                    ImGui::SetCursorPosX(160.f);
                    ImGui::TextColored(kAmber, "Required: ADM BWF/WAV file (.wav)");
                }

                ImGui::TextDisabled("LAYOUT");
                ImGui::SameLine(160.f);
                ImGui::SetNextItemWidth(110.f);
                if (ImGui::Combo("##or0layoutpreset", &mOrLayoutPreset, kLayoutNames, IM_ARRAYSIZE(kLayoutNames))) {
                    if (mOrLayoutPreset < IM_ARRAYSIZE(kLayoutNames) - 1)
                        mOrLayout = resolveProjectPath(kLayoutPaths[mOrLayoutPreset]);
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                ImGui::InputText("##or0layout", &mOrLayout);
                ImGui::SameLine();
                if (ImGui::Button("Browse##or0layout")) {
                    const std::string p = pickFile("Select Speaker Layout", {"*.json"}, "JSON files");
                    if (!p.empty()) { mOrLayout = p; mOrLayoutPreset = IM_ARRAYSIZE(kLayoutNames) - 1; }
                }
                if (mOrLayout.empty()) {
                    ImGui::SetCursorPosX(160.f);
                    ImGui::TextColored(kAmber, "Required: speaker layout JSON");
                }

                ImGui::TextDisabled("OUTPUT WAV");
                ImGui::SameLine(160.f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                ImGui::InputText("##or0out", &mOrOutput);
                ImGui::SameLine();
                if (ImGui::Button("Browse##or0out")) {
                    const std::string p = pickDirectory("Select Output Directory");
                    if (!p.empty()) mOrOutput = (fs::path(p) / "rendered_output.wav").string();
                }
                if (mOrOutput.empty()) {
                    ImGui::SetCursorPosX(160.f);
                    ImGui::TextColored(kAmber, "Required: output WAV file path");
                }

                ImGui::Spacing();
                ImGui::TextDisabled("KEEP TEMP DIR");
                ImGui::SameLine(160.f);
                ImGui::Checkbox("##or0keeptemp", &mOrKeepTempDir);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Preserve the temp dir created by CULT transcoding.\nDefault: deleted on success. Always preserved on failure.");
                ImGui::SameLine(0.f, 8.f);
                ImGui::TextDisabled("(temp dir always preserved on failure)");

                if (orBusy) ImGui::EndDisabled();

                ImGui::Spacing();
                if (ImGui::TreeNode("Advanced Options##or0adv")) {
                    if (orBusy) ImGui::BeginDisabled(true);

                    ImGui::TextDisabled("CULT OVERRIDE");
                    ImGui::SameLine(160.f);
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                    ImGui::InputText("##or0cult", &mOrCultOverride);
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##or0cult")) {
                        const std::string p = pickFile("Select cult-transcoder Binary", {}, "All files");
                        if (!p.empty()) mOrCultOverride = p;
                    }
                    ImGui::SetCursorPosX(160.f);
                    ImGui::TextDisabled("Override cult-transcoder path (normally auto-resolved by the offline renderer)");

                    ImGui::Spacing();
                    ImGui::TextColored(kAmber, "ADM source mapping convention (offline renderer):");
                    ImGui::SetCursorPosX(160.f);
                    ImGui::TextDisabled("\"N.1\" -> WAV channel N-1 (0-based).  \"LFE\" -> channel 3 when >= 4 channels.");
                    ImGui::SetCursorPosX(160.f);
                    ImGui::TextDisabled("Sources that cannot be mapped cause a hard failure; temp dir is preserved for inspection.");

                    if (orBusy) ImGui::EndDisabled();
                    ImGui::TreePop();
                }
            }
            ImGui::EndChild();

            // Build ADM command preview
            {
                const std::string rendererPath = findSpatialRenderer();
                const std::string rendererDisp = rendererPath.empty() ? "<spatialroot_spatial_render>" : rendererPath;
                const std::string admDisp    = mOrAdmInput.empty()  ? "<input.adm.wav>" : mOrAdmInput;
                const std::string layoutDisp = mOrLayout.empty()    ? "<layout.json>"   : mOrLayout;
                const std::string outDisp    = mOrOutput.empty()    ? "<output.wav>"    : mOrOutput;
                cmdPreview = rendererDisp +
                    " --adm "    + admDisp +
                    " --layout " + layoutDisp +
                    " --out "    + outDisp;
                if (mOrKeepTempDir) cmdPreview += " --keep-temp-dir";
                if (!mOrCultOverride.empty()) cmdPreview += " --cult-transcoder " + mOrCultOverride;
            }

            // ── Run controls for ADM mode ────────────────────────────────────
            if (ImGui::BeginChild("##or0ctrl", {0.f, 58.f}, true)) {
                const bool canRun = !mOrAdmInput.empty() && !mOrLayout.empty() && !mOrOutput.empty();
                if (orBusy || !canRun) ImGui::BeginDisabled(true);
                if (ImGui::Button("Render Offline — ADM WAV", {220.f, 0.f})) {
                    const std::string rendererBin = findSpatialRenderer();
                    if (rendererBin.empty()) {
                        appendOrLog("[error] spatialroot_spatial_render not found. Build with ./build.sh --offline-only");
                    } else {
                        mOrDone = false; mOrSuccess = false; mOrRunning = true;
                        try { fs::create_directories(fs::path(mOrOutput).parent_path()); } catch (...) {}
                        std::vector<std::string> args = {
                            rendererBin,
                            "--adm",    mOrAdmInput,
                            "--layout", mOrLayout,
                            "--out",    mOrOutput,
                        };
                        if (mOrKeepTempDir) args.push_back("--keep-temp-dir");
                        if (!mOrCultOverride.empty()) {
                            args.push_back("--cult-transcoder");
                            args.push_back(mOrCultOverride);
                        }
                        appendOrLog("[GUI] Running spatialroot_spatial_render (ADM WAV mode)...");
                        mOrRunner.start(args, [this](const std::string& line) { appendOrLog(line); });
                    }
                }
                if (orBusy || !canRun) ImGui::EndDisabled();

                if (!canRun && !orBusy) {
                    ImGui::SameLine();
                    ImGui::TextColored(kAmber, "Fill required fields above to enable render.");
                }

                const char* st = orBusy ? "Running..." : mOrDone ? (mOrSuccess ? "Complete" : "Failed") : "Idle";
                const ImVec4 sc = orBusy ? kAmber : mOrDone ? (mOrSuccess ? kGreen : kRed)
                                                             : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                const float sw = ImGui::CalcTextSize(st).x + 20.f;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - sw);
                ImGui::TextColored(sc, "●  %s", st);
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        // ── Tab 1: LUSID Package Render ──────────────────────────────────────
        if (ImGui::BeginTabItem("LUSID Package Render")) {
            if (ImGui::BeginChild("##or1config", {0.f, 160.f}, true)) {
                ImGui::TextDisabled("LUSID PACKAGE RENDER");
                ImGui::SameLine();
                ImGui::TextDisabled("— LUSID package (scene.lusid.json + mono stems) → offline renderer → speaker WAV");
                ImGui::Spacing();
                if (orBusy) ImGui::BeginDisabled(true);

                ImGui::TextDisabled("LUSID PACKAGE");
                ImGui::SameLine(160.f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                ImGui::InputText("##or1pkg", &mOrLusidPackage);
                ImGui::SameLine();
                if (ImGui::Button("Browse##or1pkg")) {
                    const std::string p = pickDirectory("Select LUSID Package Directory");
                    if (!p.empty()) mOrLusidPackage = p;
                }
                if (mOrLusidPackage.empty()) {
                    ImGui::SetCursorPosX(160.f);
                    ImGui::TextColored(kAmber, "Required: directory containing scene.lusid.json + mono stem WAVs");
                } else {
                    std::error_code ec;
                    if (!fs::exists(fs::path(mOrLusidPackage) / "scene.lusid.json", ec)) {
                        ImGui::SetCursorPosX(160.f);
                        ImGui::TextColored(kRed, "No scene.lusid.json found in this directory");
                    }
                }

                ImGui::TextDisabled("LAYOUT");
                ImGui::SameLine(160.f);
                ImGui::SetNextItemWidth(110.f);
                if (ImGui::Combo("##or1layoutpreset", &mOrLusidLayoutPreset, kLayoutNames, IM_ARRAYSIZE(kLayoutNames))) {
                    if (mOrLusidLayoutPreset < IM_ARRAYSIZE(kLayoutNames) - 1)
                        mOrLusidLayout = resolveProjectPath(kLayoutPaths[mOrLusidLayoutPreset]);
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                ImGui::InputText("##or1layout", &mOrLusidLayout);
                ImGui::SameLine();
                if (ImGui::Button("Browse##or1layout")) {
                    const std::string p = pickFile("Select Speaker Layout", {"*.json"}, "JSON files");
                    if (!p.empty()) { mOrLusidLayout = p; mOrLusidLayoutPreset = IM_ARRAYSIZE(kLayoutNames) - 1; }
                }
                if (mOrLusidLayout.empty()) {
                    ImGui::SetCursorPosX(160.f);
                    ImGui::TextColored(kAmber, "Required: speaker layout JSON");
                }

                ImGui::TextDisabled("OUTPUT WAV");
                ImGui::SameLine(160.f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
                ImGui::InputText("##or1out", &mOrLusidOutput);
                ImGui::SameLine();
                if (ImGui::Button("Browse##or1out")) {
                    const std::string p = pickDirectory("Select Output Directory");
                    if (!p.empty()) mOrLusidOutput = (fs::path(p) / "rendered_output.wav").string();
                }
                if (mOrLusidOutput.empty()) {
                    ImGui::SetCursorPosX(160.f);
                    ImGui::TextColored(kAmber, "Required: output WAV file path");
                }

                if (orBusy) ImGui::EndDisabled();
            }
            ImGui::EndChild();

            // Build LUSID command preview
            {
                const std::string rendererPath = findSpatialRenderer();
                const std::string rendererDisp = rendererPath.empty() ? "<spatialroot_spatial_render>" : rendererPath;
                const fs::path pkgPath = mOrLusidPackage.empty() ? fs::path("<package-dir>") : fs::path(mOrLusidPackage);
                const std::string sceneDisp  = (pkgPath / "scene.lusid.json").string();
                const std::string layoutDisp = mOrLusidLayout.empty()  ? "<layout.json>" : mOrLusidLayout;
                const std::string outDisp    = mOrLusidOutput.empty()  ? "<output.wav>"  : mOrLusidOutput;
                cmdPreview = rendererDisp +
                    " --positions " + sceneDisp +
                    " --sources "   + (mOrLusidPackage.empty() ? "<package-dir>" : mOrLusidPackage) +
                    " --layout "    + layoutDisp +
                    " --out "       + outDisp;
            }

            // ── Run controls for LUSID mode ──────────────────────────────────
            if (ImGui::BeginChild("##or1ctrl", {0.f, 58.f}, true)) {
                std::error_code ec;
                const bool hasScene = !mOrLusidPackage.empty() &&
                    fs::exists(fs::path(mOrLusidPackage) / "scene.lusid.json", ec);
                const bool canRun = hasScene && !mOrLusidLayout.empty() && !mOrLusidOutput.empty();
                if (orBusy || !canRun) ImGui::BeginDisabled(true);
                if (ImGui::Button("Render Offline — LUSID Package", {240.f, 0.f})) {
                    const std::string rendererBin = findSpatialRenderer();
                    if (rendererBin.empty()) {
                        appendOrLog("[error] spatialroot_spatial_render not found. Build with ./build.sh --offline-only");
                    } else {
                        mOrDone = false; mOrSuccess = false; mOrRunning = true;
                        try { fs::create_directories(fs::path(mOrLusidOutput).parent_path()); } catch (...) {}
                        const std::string scenePath = (fs::path(mOrLusidPackage) / "scene.lusid.json").string();
                        std::vector<std::string> args = {
                            rendererBin,
                            "--positions", scenePath,
                            "--sources",   mOrLusidPackage,
                            "--layout",    mOrLusidLayout,
                            "--out",       mOrLusidOutput,
                        };
                        appendOrLog("[GUI] Running spatialroot_spatial_render (LUSID package mode)...");
                        mOrRunner.start(args, [this](const std::string& line) { appendOrLog(line); });
                    }
                }
                if (orBusy || !canRun) ImGui::EndDisabled();

                if (!canRun && !orBusy) {
                    ImGui::SameLine();
                    ImGui::TextColored(kAmber, "Fill required fields above to enable render.");
                }

                const char* st = orBusy ? "Running..." : mOrDone ? (mOrSuccess ? "Complete" : "Failed") : "Idle";
                const ImVec4 sc = orBusy ? kAmber : mOrDone ? (mOrSuccess ? kGreen : kRed)
                                                             : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                const float sw = ImGui::CalcTextSize(st).x + 20.f;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - sw);
                ImGui::TextColored(sc, "●  %s", st);
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // ── Command preview (shared, updated by whichever tab is active) ──────────
    ImGui::Spacing();
    ImGui::TextDisabled("CMD");
    ImGui::SameLine(60.f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.08f, 0.08f, 0.08f, 1.f});
    ImGui::InputText("##orcmdpreview", &cmdPreview, ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // ── Offline render log ────────────────────────────────────────────────────
    const float logH = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginChild("##orlogcard", {0.f, logH}, true)) {
        ImGui::TextDisabled("OFFLINE RENDER LOG");
        ImGui::Spacing();
        std::deque<LogEntry> orLogSnapshot;
        {
            std::lock_guard<std::mutex> lock(mOrLogMutex);
            orLogSnapshot = mOrLog;
        }
        if (ImGui::BeginChild("##orlog", {0.f, ImGui::GetContentRegionAvail().y}, false,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            for (const auto& entry : orLogSnapshot) ImGui::TextColored(entry.color, "%s", entry.text.c_str());
            if (mOrLogAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.f) ImGui::SetScrollHereY(1.f);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

const char* App::stateName(AppState s) {
    switch (s) {
        case AppState::Idle: return "IDLE";
        case AppState::Transcoding: return "TRANSCODING";
        case AppState::Running: return "RUNNING";
        case AppState::Paused: return "PAUSED";
        case AppState::Error: return "ERROR";
    }
    return "UNKNOWN";
}

ImVec4 App::stateColor(AppState s) {
    switch (s) {
        case AppState::Idle: return {0.55f, 0.55f, 0.55f, 1.f};
        case AppState::Transcoding: return {1.f, 0.8f, 0.f, 1.f};
        case AppState::Running: return {0.2f, 0.9f, 0.2f, 1.f};
        case AppState::Paused: return {1.f, 0.6f, 0.1f, 1.f};
        case AppState::Error: return {1.f, 0.3f, 0.3f, 1.f};
    }
    return {1.f, 1.f, 1.f, 1.f};
}
