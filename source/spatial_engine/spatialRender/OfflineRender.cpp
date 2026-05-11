#include "OfflineRender.hpp"

#include "../src/JSONLoader.hpp"
#include "../src/LayoutLoader.hpp"
#include "../src/WavUtils.hpp"

#include <chrono>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#  define POPEN  _popen
#  define PCLOSE _pclose
#else
#  include <sys/wait.h>
#  define POPEN  popen
#  define PCLOSE pclose
#endif

namespace fs = std::filesystem;

namespace {
std::string quoteToken(const std::string& token) {
    std::string quoted = "\"";
    for (char c : token) {
        if (c == '"') quoted += '\\';
        quoted += c;
    }
    quoted += '"';
    return quoted;
}

std::string buildCommand(const std::vector<std::string>& tokens) {
    std::ostringstream cmd;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) cmd << ' ';
        cmd << quoteToken(tokens[i]);
    }
    cmd << " 2>&1";
    return cmd.str();
}

struct CommandResult {
    int exitCode = -1;
    std::string output;
};

CommandResult runCommandCapture(const std::vector<std::string>& tokens) {
    CommandResult result;
    FILE* pipe = POPEN(buildCommand(tokens).c_str(), "r");
    if (!pipe) {
        result.output = "Failed to launch subprocess.";
        return result;
    }

    std::array<char, 1024> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result.output += buffer.data();
    }

    int rawCode = PCLOSE(pipe);
#ifndef _WIN32
    if (WIFEXITED(rawCode)) rawCode = WEXITSTATUS(rawCode);
    else if (WIFSIGNALED(rawCode)) rawCode = 128 + WTERMSIG(rawCode);
#endif
    result.exitCode = rawCode;
    return result;
}

std::string detectInputPath(const OfflineRenderOptions& options) {
    if (!options.inputPath.empty()) return options.inputPath;
    if (!options.admPath.empty()) return options.admPath;
    if (!options.scenePath.empty()) return options.scenePath;
    return "";
}
}

const char* OfflineRenderRunner::stageName(OfflineRenderStage stage) {
    switch (stage) {
        case OfflineRenderStage::Ready: return "ready";
        case OfflineRenderStage::InputValidation: return "input validation";
        case OfflineRenderStage::AdmTranscoding: return "ADM transcoding";
        case OfflineRenderStage::SceneLoading: return "scene loading";
        case OfflineRenderStage::AudioLoading: return "audio loading";
        case OfflineRenderStage::LayoutValidation: return "layout validation";
        case OfflineRenderStage::RemapConstruction: return "remap construction";
        case OfflineRenderStage::Rendering: return "rendering";
        case OfflineRenderStage::WavWriting: return "WAV writing";
        case OfflineRenderStage::Complete: return "complete";
        case OfflineRenderStage::Cancelled: return "cancelled";
        case OfflineRenderStage::Failed: return "failed";
    }
    return "unknown";
}

bool OfflineRenderRunner::isCancelled(const OfflineRenderOptions& options) {
    return options.cancelFlag != nullptr &&
           options.cancelFlag->load(std::memory_order_relaxed);
}

void OfflineRenderRunner::report(const OfflineRenderOptions& options,
                                 OfflineRenderStage stage,
                                 float fraction,
                                 const std::string& message) {
    if (!options.progressCallback) return;
    options.progressCallback({stage, fraction, message});
}

std::string OfflineRenderRunner::createWorkingDirectory(const OfflineRenderOptions& options) {
    fs::path root;
    if (!options.tempRoot.empty()) {
        root = fs::path(options.tempRoot);
    } else {
        root = fs::temp_directory_path() / "spatialroot-offline-render";
    }

    fs::create_directories(root);
    const auto uniqueName = "job_" + std::to_string(
        static_cast<unsigned long long>(
            std::chrono::system_clock::now().time_since_epoch().count()));
    fs::path workingDir = root / uniqueName;
    fs::create_directories(workingDir);
    return workingDir.string();
}

std::string OfflineRenderRunner::runCultTranscoderPackage(const OfflineRenderOptions& options,
                                                          const std::string& admPath,
                                                          const std::string& workingDirectory,
                                                          OfflineRenderResult& result) {
    fs::path packageRoot = fs::path(workingDirectory) / "adm_package";
    fs::path reportPath = fs::path(workingDirectory) / "cult-transcoder.package.report.json";

    std::vector<std::string> command = {
        options.cultTranscoderPath.empty() ? "cult-transcoder" : options.cultTranscoderPath,
        "package-adm-wav",
        "--in", admPath,
        "--out-package", packageRoot.string(),
        "--report", reportPath.string()
    };

    CommandResult cmdResult = runCommandCapture(command);
    result.diagnosticText += "[ADM transcoding]\n" + cmdResult.output;
    if (cmdResult.exitCode != 0) {
        throw std::runtime_error("cult-transcoder package-adm-wav failed with exit code " +
                                 std::to_string(cmdResult.exitCode) + ".");
    }
    if (!fs::exists(packageRoot / "scene.lusid.json")) {
        throw std::runtime_error("cult-transcoder completed without producing scene.lusid.json.");
    }
    return packageRoot.string();
}

OfflineRenderPreparedInput OfflineRenderRunner::prepareInput(const OfflineRenderOptions& options,
                                                             OfflineRenderResult& result) {
    report(options, OfflineRenderStage::InputValidation, 0.0f, "Validating offline render input");

    if (isCancelled(options)) {
        throw std::runtime_error("cancelled");
    }

    const std::string requestedInput = detectInputPath(options);
    if (requestedInput.empty()) {
        throw std::runtime_error("No input path was provided.");
    }
    if (options.layoutPath.empty()) {
        throw std::runtime_error("No speaker layout path was provided.");
    }
    if (options.outputPath.empty()) {
        throw std::runtime_error("No output WAV path was provided.");
    }

    OfflineRenderPreparedInput prepared;
    prepared.sourcePath = requestedInput;

    std::error_code ec;
    fs::path inputPath(requestedInput);
    if (!fs::exists(inputPath, ec)) {
        throw std::runtime_error("Input path does not exist: " + requestedInput);
    }

    const std::string workingDirectory = createWorkingDirectory(options);
    result.intermediatePath = workingDirectory;

    if (!options.admPath.empty()) {
        report(options, OfflineRenderStage::AdmTranscoding, 0.0f,
               "Routing ADM input through CULT Transcoder");
        prepared.generatedPackageRoot =
            runCultTranscoderPackage(options, options.admPath, workingDirectory, result);
        prepared.scenePath = (fs::path(prepared.generatedPackageRoot) / "scene.lusid.json").string();
        prepared.sourcesFolder = prepared.generatedPackageRoot;
        prepared.packageRoot = prepared.generatedPackageRoot;
        prepared.viaAdmTranscode = true;
        return prepared;
    }

    if (fs::is_directory(inputPath, ec)) {
        fs::path scenePath = inputPath / "scene.lusid.json";
        if (!fs::exists(scenePath, ec)) {
            throw std::runtime_error("Input directory is not a LUSID package: missing scene.lusid.json.");
        }
        prepared.scenePath = scenePath.string();
        prepared.sourcesFolder = inputPath.string();
        prepared.packageRoot = inputPath.string();
        return prepared;
    }

    std::string ext = inputPath.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(c));
    if (ext == ".wav") {
        report(options, OfflineRenderStage::AdmTranscoding, 0.0f,
               "Routing ADM input through CULT Transcoder");
        prepared.generatedPackageRoot =
            runCultTranscoderPackage(options, requestedInput, workingDirectory, result);
        prepared.scenePath = (fs::path(prepared.generatedPackageRoot) / "scene.lusid.json").string();
        prepared.sourcesFolder = prepared.generatedPackageRoot;
        prepared.packageRoot = prepared.generatedPackageRoot;
        prepared.viaAdmTranscode = true;
        return prepared;
    }

    prepared.scenePath = !options.scenePath.empty() ? options.scenePath : requestedInput;
    if (!options.sourcesFolder.empty()) {
        prepared.sourcesFolder = options.sourcesFolder;
    } else {
        prepared.sourcesFolder = inputPath.parent_path().string();
    }

    if (prepared.sourcesFolder.empty()) {
        throw std::runtime_error("A sources folder is required when rendering from a standalone scene file.");
    }
    return prepared;
}

OfflineRenderResult OfflineRenderRunner::run(const OfflineRenderOptions& options) {
    OfflineRenderResult result;
    result.inputPath = detectInputPath(options);
    result.layoutPath = options.layoutPath;
    result.outputPath = options.outputPath;
    result.stage = OfflineRenderStage::InputValidation;

    try {
        const OfflineRenderPreparedInput prepared = prepareInput(options, result);
        result.scenePath = prepared.scenePath;
        result.sourcesFolder = prepared.sourcesFolder;

        if (isCancelled(options)) {
            result.cancelled = true;
            result.stage = OfflineRenderStage::Cancelled;
            return result;
        }

        result.stage = OfflineRenderStage::SceneLoading;
        report(options, OfflineRenderStage::SceneLoading, 0.0f,
               "Loading LUSID scene");
        SpatialData spatial = JSONLoader::loadLusidScene(prepared.scenePath);

        result.stage = OfflineRenderStage::AudioLoading;
        report(options, OfflineRenderStage::AudioLoading, 0.0f,
               "Loading prepared audio sources");
        std::map<std::string, MonoWavData> sources =
            WavUtils::loadSources(prepared.sourcesFolder, spatial.sources, spatial.sampleRate);

        if (isCancelled(options)) {
            result.cancelled = true;
            result.stage = OfflineRenderStage::Cancelled;
            return result;
        }

        result.stage = OfflineRenderStage::LayoutValidation;
        report(options, OfflineRenderStage::LayoutValidation, 0.0f,
               "Loading speaker layout");
        SpeakerLayoutData layout = LayoutLoader::loadLayout(options.layoutPath);

        result.stage = OfflineRenderStage::RemapConstruction;
        report(options, OfflineRenderStage::RemapConstruction, 0.0f,
               "Building offline device-indexed remap plan");
        OfflineRenderRemapPlan remapPlan = OfflineRenderRemapPlan::build(layout);
        result.warnings.insert(result.warnings.end(),
                               remapPlan.warnings().begin(),
                               remapPlan.warnings().end());

        SpatialRenderer renderer(layout, spatial, sources);
        RenderConfig renderConfig;
        renderConfig.masterGainDb = options.masterGainDb;
        renderConfig.dbapFocus = options.dbapFocus;
        renderConfig.speakerMixDb = options.loudspeakerMixDb;
        renderConfig.subMixDb = options.subMixDb;
        renderConfig.elevationMode = options.elevationMode;
        renderConfig.blockSize = options.blockSize;
        renderConfig.debugDiagnostics = options.debugDiagnostics;
        renderConfig.debugOutputDir = options.debugOutputDir;
        renderConfig.renderResolution = "block";
        renderConfig.pannerType = PannerType::DBAP;

        result.stage = OfflineRenderStage::Rendering;
        report(options, OfflineRenderStage::Rendering, 0.0f,
               "Rendering to internal bus");
        MultiWavData internal = renderer.render(
            renderConfig,
            [&options](float fraction, const std::string& message) {
                OfflineRenderRunner::report(options, OfflineRenderStage::Rendering, fraction, message);
            },
            options.cancelFlag);

        if (renderer.wasCancelled() || isCancelled(options)) {
            result.cancelled = true;
            result.stage = OfflineRenderStage::Cancelled;
            report(options, OfflineRenderStage::Cancelled, 1.0f,
                   "Offline render cancelled");
            return result;
        }

        MultiWavData deviceIndexed = remapPlan.scatterToDeviceIndexed(internal);

        result.stage = OfflineRenderStage::WavWriting;
        report(options, OfflineRenderStage::WavWriting, 0.95f,
               "Writing device-indexed WAV");
        WavUtils::writeMultichannelWav(options.outputPath, deviceIndexed);

        result.success = true;
        result.stage = OfflineRenderStage::Complete;
        report(options, OfflineRenderStage::Complete, 1.0f,
               "Offline render complete");
        return result;
    } catch (const std::exception& e) {
        if (std::string(e.what()) == "cancelled") {
            result.cancelled = true;
            result.stage = OfflineRenderStage::Cancelled;
            report(options, OfflineRenderStage::Cancelled, 1.0f,
                   "Offline render cancelled");
            return result;
        }
        result.success = false;
        result.stage = OfflineRenderStage::Failed;
        result.errorMessage = e.what();

        std::ostringstream oss;
        oss << "Stage: " << stageName(result.stage) << "\n";
        oss << "Input: " << result.inputPath << "\n";
        if (!result.scenePath.empty()) oss << "Scene: " << result.scenePath << "\n";
        if (!result.sourcesFolder.empty()) oss << "Sources: " << result.sourcesFolder << "\n";
        if (!result.layoutPath.empty()) oss << "Layout: " << result.layoutPath << "\n";
        if (!result.outputPath.empty()) oss << "Output: " << result.outputPath << "\n";
        if (!result.intermediatePath.empty()) oss << "Intermediate: " << result.intermediatePath << "\n";
        oss << "Error: " << result.errorMessage << "\n";
        if (!result.diagnosticText.empty()) {
            oss << result.diagnosticText;
            if (result.diagnosticText.back() != '\n') oss << "\n";
        }
        result.diagnosticText = oss.str();

        report(options, OfflineRenderStage::Failed, 1.0f,
               "Offline render failed: " + result.errorMessage);
        return result;
    }
}
