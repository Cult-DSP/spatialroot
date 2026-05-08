#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct TempSessionManifest {
    std::string sessionId;
    std::string createdAtUtc;
    std::string sourcePath;
    std::string sessionType;
    std::string status;
    bool        saved      = false;
    bool        preserved  = false;
};

class SpatialRootPaths {
public:
    static std::filesystem::path defaultCacheRoot();
    static std::filesystem::path cacheRoot(const std::string& overrideRoot = "");
    static std::filesystem::path tempSessionsRoot(const std::string& overrideRoot = "");
    static std::filesystem::path createTempSessionRoot(const std::string& overrideRoot = "");
    static std::string           makeSessionId();
    static std::string           makeCreatedAtUtc();

    static void ensureTempSessionLayout(const std::filesystem::path& sessionRoot);
    static void writeTempSessionMarker(const std::filesystem::path& sessionRoot);
    static void writeManifest(const std::filesystem::path& sessionRoot,
                              const TempSessionManifest& manifest);

    static bool isSafeTempSessionPath(const std::filesystem::path& candidate,
                                      const std::filesystem::path& tempSessionsRoot);
    static bool shouldDeleteTempSession(const std::filesystem::path& sessionRoot,
                                        const std::filesystem::path& tempSessionsRoot);
    static bool deleteTempSession(const std::filesystem::path& sessionRoot,
                                  const std::filesystem::path& tempSessionsRoot,
                                  std::uintmax_t* removedCount = nullptr);
    static void copySessionContents(const std::filesystem::path& sessionRoot,
                                    const std::filesystem::path& destinationRoot);

private:
    static std::filesystem::path homeDirectory();
    static std::filesystem::path normalizeForComparison(const std::filesystem::path& path);
    static std::string escapeJson(const std::string& value);
};
