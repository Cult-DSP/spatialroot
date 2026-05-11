#include "OfflineRenderCliCommand.hpp"

#include "OfflineRender.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>

namespace {
std::string getArgString(int argc, char* argv[], const std::string& flag) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == flag) return std::string(argv[i + 1]);
    }
    return "";
}

bool hasArg(int argc, char* argv[], const std::string& flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == flag) return true;
    }
    return false;
}

int getArgInt(int argc, char* argv[], const std::string& flag, int defaultValue) {
    const std::string value = getArgString(argc, argv, flag);
    if (value.empty()) return defaultValue;
    try { return std::stoi(value); } catch (...) { return defaultValue; }
}

float getArgFloatWithAliases(int argc, char* argv[],
                             const std::vector<std::string>& flags,
                             float defaultValue) {
    for (const auto& flag : flags) {
        const std::string value = getArgString(argc, argv, flag);
        if (!value.empty()) {
            try { return std::stof(value); } catch (...) { return defaultValue; }
        }
    }
    return defaultValue;
}

OfflineElevationMode parseElevationMode(const std::string& rawValue) {
    if (rawValue.empty()) return OfflineElevationMode::RescaleAtmosUp;

    std::string value = rawValue;
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (value == "0" || value == "rescaleatmosup" || value == "rescale_atmos_up") {
        return OfflineElevationMode::RescaleAtmosUp;
    }
    if (value == "1" || value == "rescalefullsphere" || value == "rescale_full_sphere" ||
        value == "fullsphere") {
        return OfflineElevationMode::RescaleFullSphere;
    }
    if (value == "2" || value == "clamp") {
        return OfflineElevationMode::Clamp;
    }
    throw std::runtime_error("Unknown elevation mode: " + rawValue);
}

void printUsage(const char* progName) {
    std::cout
        << "Spatial Root Offline Renderer\n\n"
        << "Usage:\n"
        << "  " << progName << " --input <scene-or-package>\n"
        << "             --layout <layout.json>\n"
        << "             --output <render.wav>\n"
        << "             [--sources <folder>]\n"
        << "             [--master-gain <dB>] [--focus <value>]\n"
        << "             [--loudspeaker-mix <dB>] [--sub-mix <dB>]\n"
        << "             [--elevation-mode RescaleAtmosUp|RescaleFullSphere|Clamp]\n\n"
        << "ADM route:\n"
        << "  " << progName << " --adm <source.wav>\n"
        << "             --layout <layout.json>\n"
        << "             --output <render.wav>\n"
        << "             [--cult-transcoder <path>]\n\n"
        << "Compatibility aliases are also accepted:\n"
        << "  --positions / --scene, --out, --master_gain, --speaker_mix,\n"
        << "  --sub_mix, --dbap_focus, --elevation_mode, --block_size.\n";
}
}

int OfflineRenderCliCommand::run(int argc, char* argv[]) {
    if (hasArg(argc, argv, "--help") || hasArg(argc, argv, "-h")) {
        printUsage(argv[0]);
        return 0;
    }

    OfflineRenderOptions options;
    options.inputPath = getArgString(argc, argv, "--input");
    options.scenePath = getArgString(argc, argv, "--scene");
    if (options.scenePath.empty()) options.scenePath = getArgString(argc, argv, "--positions");
    options.sourcesFolder = getArgString(argc, argv, "--sources");
    options.admPath = getArgString(argc, argv, "--adm");
    options.layoutPath = getArgString(argc, argv, "--layout");
    options.outputPath = getArgString(argc, argv, "--output");
    if (options.outputPath.empty()) options.outputPath = getArgString(argc, argv, "--out");
    options.cultTranscoderPath = getArgString(argc, argv, "--cult-transcoder");
    options.tempRoot = getArgString(argc, argv, "--temp-root");
    options.debugOutputDir = getArgString(argc, argv, "--debug-dir");
    options.debugDiagnostics = !options.debugOutputDir.empty();

    if (options.inputPath.empty() && !options.scenePath.empty()) options.inputPath = options.scenePath;
    if (options.inputPath.empty() && !options.admPath.empty()) options.inputPath = options.admPath;

    options.masterGainDb = getArgFloatWithAliases(argc, argv,
        {"--master-gain", "--master_gain", "--gain"}, 0.0f);
    options.dbapFocus = getArgFloatWithAliases(argc, argv,
        {"--focus", "--dbap_focus"}, 1.5f);
    options.loudspeakerMixDb = getArgFloatWithAliases(argc, argv,
        {"--loudspeaker-mix", "--speaker_mix"}, 0.0f);
    options.subMixDb = getArgFloatWithAliases(argc, argv,
        {"--sub-mix", "--sub_mix"}, 0.0f);
    options.blockSize = getArgInt(argc, argv, "--block-size", 64);
    if (hasArg(argc, argv, "--block_size")) {
        options.blockSize = getArgInt(argc, argv, "--block_size", options.blockSize);
    }

    try {
        const std::string elevationValue =
            getArgString(argc, argv, "--elevation-mode").empty()
                ? getArgString(argc, argv, "--elevation_mode")
                : getArgString(argc, argv, "--elevation-mode");
        options.elevationMode = parseElevationMode(elevationValue);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    if (options.layoutPath.empty() || options.outputPath.empty() ||
        (options.inputPath.empty() && options.admPath.empty())) {
        std::cerr << "Error: missing required arguments.\n\n";
        printUsage(argv[0]);
        return 1;
    }

    options.progressCallback = [](const OfflineRenderProgress& progress) {
        std::cout << "[" << OfflineRenderRunner::stageName(progress.stage) << "] "
                  << progress.message;
        if (progress.stage == OfflineRenderStage::Rendering) {
            std::cout << " (" << static_cast<int>(progress.fraction * 100.0f) << "%)";
        }
        std::cout << "\n";
    };

    const OfflineRenderResult result = OfflineRenderRunner::run(options);
    if (!result.success) {
        if (result.cancelled) {
            std::cerr << "Offline render cancelled.\n";
        } else {
            std::cerr << "Offline render failed at "
                      << OfflineRenderRunner::stageName(result.stage)
                      << ": " << result.errorMessage << "\n";
            if (!result.diagnosticText.empty()) {
                std::cerr << result.diagnosticText;
                if (result.diagnosticText.back() != '\n') std::cerr << "\n";
            }
        }
        return 1;
    }

    for (const auto& warning : result.warnings) {
        std::cerr << "Warning: " << warning << "\n";
    }
    std::cout << "Offline render complete: " << result.outputPath << "\n";
    return 0;
}
