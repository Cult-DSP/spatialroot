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
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

constexpr int         App::kBufferSizes[];
constexpr const char* App::kBufferSizeNames[];
constexpr const char* App::kLayoutNames[];
constexpr const char* App::kLayoutPaths[];
constexpr const char* App::kElevModeNames[];
constexpr const char* App::kTcFormatNames[];
constexpr const char* App::kTcFormatValues[];
constexpr const char* App::kTcLfeModeNames[];
constexpr const char* App::kTcLfeModeValues[];

App::App(std::string projectRoot, bool keepTempSessions, std::string tempRootOverride)
    : mProjectRoot(std::move(projectRoot))
    , mKeepTempSessions(keepTempSessions)
    , mTempRootOverride(std::move(tempRootOverride))
    , mSession(std::make_unique<EngineSession>()) {
    mLayoutPath = resolveProjectPath(kLayoutPaths[0]);

    appendEngineLog("[GUI] Spatial Root — ImGui + GLFW GUI started.");
    appendEngineLog("[GUI] Project root: " + mProjectRoot);
    appendEngineLog("[GUI] Temp session root: " + pathString(tempSessionsRoot()));
    if (mKeepTempSessions) {
        appendEngineLog("[GUI] Keeping temporary sessions for debugging is enabled.",
                        {1.f, 0.8f, 0.2f, 1.f});
    }
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
}

void App::requestShutdown() {
    if (mShutdownRequested) return;
    mShutdownRequested = true;
    if (mState == AppState::Running || mState == AppState::Paused) {
        appendEngineLog("[GUI] Shutting down engine...");
        mSession->shutdown();
        mState = AppState::Idle;
    }
    cleanupOwnedTempSessions();
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

    if (ImGui::BeginChild("##inputcard", {0.f, 266.f}, true)) {
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
            if (mLayoutPreset < 2) mLayoutPath = resolveProjectPath(kLayoutPaths[mLayoutPreset]);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
        ImGui::InputText("##layout", &mLayoutPath);
        ImGui::SameLine();
        if (ImGui::Button("Browse##layout")) {
            const std::string p = pickFile("Select Speaker Layout", {"*.json"}, "JSON files");
            if (!p.empty()) { mLayoutPath = p; mLayoutPreset = 2; }
        }

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

        if (isRunning) ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::Spacing();

    if (ImGui::BeginChild("##transportcard", {0.f, 132.f}, true)) {
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
        if (ImGui::Button("Save Generated Scene...")) {
            saveSessionCopy(*mActiveTempSessionRoot, mActiveTempManifest,
                            "Choose Folder for Generated Scene",
                            "Generated scene saved");
        }
        if (!mLastGeneratedSceneAvailable || !mActiveTempSessionRoot) ImGui::EndDisabled();

        ImGui::SameLine();
        const bool hasDiagnostics = (mLastFailureHasDiagnostics &&
                                     ((mActiveTempSessionRoot.has_value()) || (mTcTempSessionRoot.has_value())));
        if (!hasDiagnostics) ImGui::BeginDisabled(true);
        if (ImGui::Button("Save Diagnostic Files...")) {
            if (mActiveTempSessionRoot) {
                saveSessionCopy(*mActiveTempSessionRoot, mActiveTempManifest,
                                "Choose Folder for Diagnostic Files",
                                "Diagnostic files saved");
            } else if (mTcTempSessionRoot) {
                saveSessionCopy(*mTcTempSessionRoot, mTcTempManifest,
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

    if (ImGui::BeginChild("##ctrlcard", {0.f, 220.f}, true)) {
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
    const ImVec4 kRed = {0.72f, 0.18f, 0.15f, 1.f};
    const bool tcBusy = mTcRunner.isRunning();

    if (ImGui::BeginChild("##tcconfigcard", {0.f, 186.f}, true)) {
        ImGui::TextDisabled("TRANSCODE CONFIGURATION");
        ImGui::Spacing();
        if (tcBusy) ImGui::BeginDisabled(true);

        ImGui::TextDisabled("INPUT FILE");
        ImGui::SameLine(130.f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 78.f);
        ImGui::InputText("##tcinput", &mTcInput);
        ImGui::SameLine();
        if (ImGui::Button("Browse##tcinput")) {
            const std::string p = pickFile("Select ADM Input", {"*.wav", "*.xml"}, "ADM files");
            if (!p.empty()) mTcInput = p;
        }

        ImGui::TextDisabled("IN-FORMAT");
        ImGui::SameLine(130.f);
        ImGui::SetNextItemWidth(140.f);
        ImGui::Combo("##tcinformat", &mTcInFormat, kTcFormatNames, 4);

        ImGui::TextDisabled("LFE MODE");
        ImGui::SameLine(130.f);
        ImGui::SetNextItemWidth(140.f);
        ImGui::Combo("##tclfemode", &mTcLfeMode, kTcLfeModeNames, 2);

        ImGui::TextDisabled("OUTPUT");
        ImGui::SameLine(130.f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 8.f);
        ImGui::InputText("##tcoutput", &mTcOutput);
        ImGui::SameLine(130.f);
        ImGui::TextDisabled("(empty = temporary session in app cache)");

        ImGui::TextDisabled("TEMP FILES");
        ImGui::SameLine(130.f);
        ImGui::Checkbox("Keep temporary generated files for debugging", &mKeepTempSessions);

        if (tcBusy) ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::Spacing();

    if (ImGui::BeginChild("##tcctrlcard", {0.f, 80.f}, true)) {
        ImGui::TextDisabled("CONTROL");
        ImGui::Spacing();

        if (tcBusy) ImGui::BeginDisabled(true);
        if (ImGui::Button("Transcode", {120.f, 0.f})) {
            std::string format = kTcFormatValues[mTcInFormat];
            if (mTcInFormat == 0) {
                std::string lower = mTcInput;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".xml") format = "adm_xml";
                else format = "adm_wav";
            }

            const std::string cultBin = findCultTranscoder();
            if (cultBin.empty()) {
                appendTcLog("[error] cult-transcoder binary not found. Build with ./build.sh first.");
            } else if (mTcInput.empty()) {
                appendTcLog("[error] Input path is required.");
            } else {
                clearStandaloneTranscodeTempState();
                mTcDone = false;
                mTcSuccess = false;
                mTcRunning = true;

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
                    cultBin, "transcode", "--in", mTcInput, "--in-format", format,
                    "--out", outputPath.string(), "--out-format", "lusid_json",
                    "--report", reportPath.string(), "--lfe-mode", kTcLfeModeValues[mTcLfeMode],
                };
                appendTcLog("[GUI] Running: " + cultBin + " transcode ...");
                mTcRunner.start(args, [this](const std::string& line) { appendTcLog(line); });
            }
        }
        if (tcBusy) ImGui::EndDisabled();

        const char* tcStatus = tcBusy ? "Running..." : mTcDone ? (mTcSuccess ? "Complete" : "Failed") : "Idle";
        const ImVec4 tcColor = tcBusy ? kAmber : mTcDone ? (mTcSuccess ? kGreen : kRed)
                                                    : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        const float statusW = ImGui::CalcTextSize(tcStatus).x + 20.f;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - statusW);
        ImGui::TextColored(tcColor, "●  %s", tcStatus);
    }
    ImGui::EndChild();
    ImGui::Spacing();

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
        mSession->shutdown();
        return;
    }

    if (!mSession->start()) {
        mLastError = mSession->getLastError();
        mState = AppState::Error;
        appendEngineLog("[Engine] start() failed: " + mLastError, {1.f, 0.4f, 0.4f, 1.f});
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
        resolveProjectPath("build/cult_transcoder/cult-transcoder"),
        resolveProjectPath("cult_transcoder/build/cult-transcoder"),
    };
#ifdef _WIN32
    for (auto& c : candidates) c += ".exe";
#endif
    for (const auto& c : candidates) {
        if (fs::exists(c)) return c;
    }
    return "";
}

std::string App::transcodeOutputPath(const std::string& admPath) const {
    const fs::path p(admPath);
    return resolveProjectPath("processedData/stageForRender/" + p.stem().string() + ".lusid.json");
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

bool App::saveSessionCopy(const fs::path& sessionRoot, TempSessionManifest& manifest,
                          const std::string& dialogTitle, const std::string& successLabel) {
    const std::string destination = pickDirectory(dialogTitle);
    if (destination.empty()) return false;
    try {
        SpatialRootPaths::copySessionContents(sessionRoot, fs::path(destination));
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
