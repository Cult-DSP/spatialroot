#pragma once

#include <filesystem>
#include <string>

namespace StartupLogger {

void initialize(const std::filesystem::path& appRoot);
void append(const std::string& line);
std::filesystem::path logPath();

}  // namespace StartupLogger
