#pragma once

#include "SpatialRenderer.hpp"
#include "../src/OfflineRenderRemapPlan.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

enum class OfflineRenderStage {
    Ready,
    InputValidation,
    AdmTranscoding,
    SceneLoading,
    AudioLoading,
    LayoutValidation,
    RemapConstruction,
    Rendering,
    WavWriting,
    Complete,
    Cancelled,
    Failed
};

struct OfflineRenderProgress {
    OfflineRenderStage stage = OfflineRenderStage::Ready;
    float fraction = 0.0f;
    std::string message;
};

struct OfflineRenderPreparedInput {
    std::string sourcePath;
    std::string scenePath;
    std::string sourcesFolder;
    std::string packageRoot;
    std::string generatedPackageRoot;
    bool viaAdmTranscode = false;
};

struct OfflineRenderOptions {
    std::string inputPath;
    std::string scenePath;
    std::string sourcesFolder;
    std::string admPath;
    std::string layoutPath;
    std::string outputPath;
    std::string cultTranscoderPath;
    std::string tempRoot;
    std::string debugOutputDir;

    float masterGainDb = 0.0f;
    float dbapFocus = 1.5f;
    float loudspeakerMixDb = 0.0f;
    float subMixDb = 0.0f;
    OfflineElevationMode elevationMode = OfflineElevationMode::RescaleAtmosUp;
    int blockSize = 64;
    bool debugDiagnostics = false;

    const std::atomic<bool>* cancelFlag = nullptr;
    std::function<void(const OfflineRenderProgress&)> progressCallback;
};

struct OfflineRenderResult {
    bool success = false;
    bool cancelled = false;
    OfflineRenderStage stage = OfflineRenderStage::Ready;
    std::string errorMessage;
    std::string diagnosticText;
    std::string inputPath;
    std::string scenePath;
    std::string sourcesFolder;
    std::string layoutPath;
    std::string outputPath;
    std::string intermediatePath;
    std::vector<std::string> warnings;
};

class OfflineRenderRunner {
public:
    static OfflineRenderResult run(const OfflineRenderOptions& options);
    static const char* stageName(OfflineRenderStage stage);

private:
    static bool isCancelled(const OfflineRenderOptions& options);
    static void report(const OfflineRenderOptions& options,
                       OfflineRenderStage stage,
                       float fraction,
                       const std::string& message);
    static OfflineRenderPreparedInput prepareInput(const OfflineRenderOptions& options,
                                                   OfflineRenderResult& result);
    static std::string createWorkingDirectory(const OfflineRenderOptions& options);
    static std::string runCultTranscoderPackage(const OfflineRenderOptions& options,
                                                const std::string& admPath,
                                                const std::string& workingDirectory,
                                                OfflineRenderResult& result);
};
