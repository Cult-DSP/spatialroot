#pragma once

#include <filesystem>
#include <string>

#include <sndfile.h>

namespace spatialroot {

inline std::filesystem::path pathFromString(const std::string& path) {
#ifdef _WIN32
    return std::filesystem::u8path(path);
#else
    return std::filesystem::path(path);
#endif
}

inline SNDFILE* openSndFileRead(const std::filesystem::path& path, SF_INFO* info) {
#ifdef _WIN32
    return sf_wchar_open(path.c_str(), SFM_READ, info);
#else
    return sf_open(path.string().c_str(), SFM_READ, info);
#endif
}

inline SNDFILE* openSndFileRead(const std::string& path, SF_INFO* info) {
    return openSndFileRead(pathFromString(path), info);
}

inline SNDFILE* openSndFileWrite(const std::filesystem::path& path, SF_INFO* info) {
#ifdef _WIN32
    return sf_wchar_open(path.c_str(), SFM_WRITE, info);
#else
    return sf_open(path.string().c_str(), SFM_WRITE, info);
#endif
}

inline SNDFILE* openSndFileWrite(const std::string& path, SF_INFO* info) {
    return openSndFileWrite(pathFromString(path), info);
}

}  // namespace spatialroot
