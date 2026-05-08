#include "SpatialRootPaths.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
constexpr const char* kTempMarker = ".spatialroot_temp_session";

std::string utcTimestampForName() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto tt = clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
    return oss.str();
}

std::string utcTimestampForManifest() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto tt = clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}
}

fs::path SpatialRootPaths::homeDirectory() {
#ifdef _WIN32
    if (const char* userProfile = std::getenv("USERPROFILE")) return userProfile;
    const char* drive = std::getenv("HOMEDRIVE");
    const char* path = std::getenv("HOMEPATH");
    if (drive && path) return std::string(drive) + path;
#else
    if (const char* home = std::getenv("HOME")) return home;
#endif
    return {};
}

fs::path SpatialRootPaths::defaultCacheRoot() {
#ifdef _WIN32
    if (const char* localAppData = std::getenv("LOCALAPPDATA")) {
        return fs::path(localAppData) / "CultDSP" / "SpatialRoot" / "Cache";
    }
    return homeDirectory() / "AppData" / "Local" / "CultDSP" / "SpatialRoot" / "Cache";
#elif defined(__APPLE__)
    return homeDirectory() / "Library" / "Caches" / "CultDSP" / "SpatialRoot";
#else
    if (const char* xdgCache = std::getenv("XDG_CACHE_HOME"); xdgCache && *xdgCache) {
        return fs::path(xdgCache) / "CultDSP" / "SpatialRoot";
    }
    return homeDirectory() / ".cache" / "CultDSP" / "SpatialRoot";
#endif
}

fs::path SpatialRootPaths::cacheRoot(const std::string& overrideRoot) {
    if (!overrideRoot.empty()) return fs::path(overrideRoot);
    if (const char* envOverride = std::getenv("SPATIALROOT_TEMP_ROOT");
        envOverride && *envOverride) {
        return fs::path(envOverride);
    }
    return defaultCacheRoot();
}

fs::path SpatialRootPaths::tempSessionsRoot(const std::string& overrideRoot) {
    return cacheRoot(overrideRoot) / "temp-sessions";
}

std::string SpatialRootPaths::makeSessionId() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    static constexpr char hex[] = "0123456789abcdef";
    std::string shortId;
    shortId.reserve(8);
    for (int i = 0; i < 8; ++i) shortId.push_back(hex[rng() % 16]);
    return "session_" + utcTimestampForName() + "_" + shortId;
}

std::string SpatialRootPaths::makeCreatedAtUtc() {
    return utcTimestampForManifest();
}

fs::path SpatialRootPaths::createTempSessionRoot(const std::string& overrideRoot) {
    fs::path root = tempSessionsRoot(overrideRoot) / makeSessionId();
    ensureTempSessionLayout(root);
    return root;
}

void SpatialRootPaths::ensureTempSessionLayout(const fs::path& sessionRoot) {
    fs::create_directories(sessionRoot);
    writeTempSessionMarker(sessionRoot);
}

void SpatialRootPaths::writeTempSessionMarker(const fs::path& sessionRoot) {
    std::ofstream out(sessionRoot / kTempMarker, std::ios::trunc);
    out << "Spatial Root temporary session\n";
}

void SpatialRootPaths::writeManifest(const fs::path& sessionRoot,
                                     const TempSessionManifest& manifest) {
    std::ofstream out(sessionRoot / "manifest.json", std::ios::trunc);
    out << "{\n"
        << "  \"sessionId\": \"" << escapeJson(manifest.sessionId) << "\",\n"
        << "  \"createdAtUtc\": \"" << escapeJson(manifest.createdAtUtc) << "\",\n"
        << "  \"sourcePath\": \"" << escapeJson(manifest.sourcePath) << "\",\n"
        << "  \"sessionType\": \"" << escapeJson(manifest.sessionType) << "\",\n"
        << "  \"status\": \"" << escapeJson(manifest.status) << "\",\n"
        << "  \"saved\": " << (manifest.saved ? "true" : "false") << ",\n"
        << "  \"preserved\": " << (manifest.preserved ? "true" : "false") << "\n"
        << "}\n";
}

fs::path SpatialRootPaths::normalizeForComparison(const fs::path& path) {
    if (path.empty()) return {};
    std::error_code ec;
    fs::path weak = fs::weakly_canonical(path, ec);
    if (!ec) return weak.lexically_normal();
    return fs::absolute(path, ec).lexically_normal();
}

bool SpatialRootPaths::isSafeTempSessionPath(const fs::path& candidate,
                                             const fs::path& tempSessionsRoot) {
    if (candidate.empty()) return false;

    const fs::path session = normalizeForComparison(candidate);
    const fs::path sessionsRoot = normalizeForComparison(tempSessionsRoot);
    const fs::path home = normalizeForComparison(homeDirectory());
    const fs::path cwd = normalizeForComparison(fs::current_path());

    if (session.empty() || sessionsRoot.empty()) return false;
    if (session == sessionsRoot) return false;
    if (session == session.root_path()) return false;
    if (!home.empty() && session == home) return false;
    if (!cwd.empty() && session == cwd) return false;
    if (!cwd.empty() && (session == cwd / "sourceData" || session == cwd / "processedData")) return false;

    auto rel = session.lexically_relative(sessionsRoot);
    if (rel.empty() || rel.native().empty() || rel.string() == "." || rel.string().rfind("..", 0) == 0) {
        return false;
    }
    if (session.filename().string().rfind("session_", 0) != 0) return false;
    return true;
}

bool SpatialRootPaths::shouldDeleteTempSession(const fs::path& sessionRoot,
                                               const fs::path& tempSessionsRoot) {
    if (!isSafeTempSessionPath(sessionRoot, tempSessionsRoot)) return false;
    std::error_code ec;
    return fs::exists(sessionRoot / kTempMarker, ec) && fs::is_regular_file(sessionRoot / kTempMarker, ec);
}

bool SpatialRootPaths::deleteTempSession(const fs::path& sessionRoot,
                                         const fs::path& tempSessionsRoot,
                                         std::uintmax_t* removedCount) {
    if (!shouldDeleteTempSession(sessionRoot, tempSessionsRoot)) return false;
    std::error_code ec;
    const auto removed = fs::remove_all(sessionRoot, ec);
    if (removedCount) *removedCount = removed;
    return !ec;
}

void SpatialRootPaths::copySessionContents(const fs::path& sessionRoot,
                                           const fs::path& destinationRoot) {
    fs::create_directories(destinationRoot);
    for (const auto& entry : fs::directory_iterator(sessionRoot)) {
        const fs::path dest = destinationRoot / entry.path().filename();
        std::error_code ec;
        if (entry.is_directory(ec)) {
            fs::copy(entry.path(), dest,
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        } else if (entry.is_regular_file(ec)) {
            fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);
        }
    }
}

std::string SpatialRootPaths::escapeJson(const std::string& value) {
    std::ostringstream oss;
    for (unsigned char c : value) {
        switch (c) {
            case '\\': oss << "\\\\"; break;
            case '"': oss << "\\\""; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 0x20) {
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c
                        << std::dec << std::setfill(' ');
                } else {
                    oss << static_cast<char>(c);
                }
        }
    }
    return oss.str();
}
