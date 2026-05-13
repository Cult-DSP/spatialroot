#include "StartupLogger.hpp"

#include <fstream>
#include <mutex>

namespace fs = std::filesystem;

namespace StartupLogger {
namespace {
std::mutex gMutex;
fs::path   gLogPath;
}

void initialize(const fs::path& appRoot) {
    std::lock_guard<std::mutex> lock(gMutex);

    std::error_code ec;
    fs::path root = appRoot;
    if (root.empty()) root = fs::current_path(ec);
    if (root.empty()) root = ".";

    fs::create_directories(root, ec);
    gLogPath = root / "SpatialRoot-startup.log";

    std::ofstream out(gLogPath, std::ios::trunc);
    out << "Spatial Root startup log\n";
}

void append(const std::string& line) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gLogPath.empty()) return;

    std::ofstream out(gLogPath, std::ios::app);
    if (!out) return;
    out << line << '\n';
}

fs::path logPath() {
    std::lock_guard<std::mutex> lock(gMutex);
    return gLogPath;
}
}  // namespace StartupLogger
