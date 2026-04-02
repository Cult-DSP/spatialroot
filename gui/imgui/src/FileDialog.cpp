#include "FileDialog.hpp"

#include <cstdio>

// macOS: implemented in FileDialog_macOS.mm (NSOpenPanel via Objective-C++).
// Windows and Linux implementations follow.

// ─────────────────────────────────────────────────────────────────────────────
// Windows — Win32 OPENFILENAME dialog
// ─────────────────────────────────────────────────────────────────────────────
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>    // GetOpenFileName — linked via Comdlg32 (see CMakeLists.txt)

std::string pickFile(const std::string&              title,
                     const std::vector<std::string>& filterPatterns,
                     const std::string&              filterDescription) {
    // Build double-null-terminated filter string: "Description\0*.ext1;*.ext2\0\0"
    std::string filterStr;
    if (!filterDescription.empty() && !filterPatterns.empty()) {
        filterStr += filterDescription;
        filterStr += '\0';
        for (size_t i = 0; i < filterPatterns.size(); ++i) {
            if (i > 0) filterStr += ';';
            filterStr += filterPatterns[i];
        }
        filterStr += '\0';
    }
    filterStr += '\0';  // double null terminate

    char szFile[MAX_PATH] = {};
    OPENFILENAMEA ofn    = {};
    ofn.lStructSize      = sizeof(ofn);
    ofn.lpstrFilter      = filterStr.size() > 1 ? filterStr.c_str() : nullptr;
    ofn.lpstrFile        = szFile;
    ofn.nMaxFile         = MAX_PATH;
    ofn.lpstrTitle       = title.c_str();
    ofn.Flags            = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn))
        return std::string(szFile);
    return {};
}

std::string pickFileOrDirectory(const std::string& title) {
    // No standard Win32 combined file+folder dialog without COM/IFileOpenDialog.
    // Fall back to file-only; directories must be typed.
    return pickFile(title, {}, "All files");
}

// ─────────────────────────────────────────────────────────────────────────────
// Linux — zenity --file-selection
// Falls back to empty string if zenity is not installed.
// ─────────────────────────────────────────────────────────────────────────────
#elif !defined(__APPLE__)

std::string pickFile(const std::string&              title,
                     const std::vector<std::string>& filterPatterns,
                     const std::string&              filterDescription) {
    std::string cmd = "zenity --file-selection --title=\"" + title + "\"";
    if (!filterDescription.empty()) {
        cmd += " --file-filter=\"" + filterDescription;
        for (const auto& p : filterPatterns)
            cmd += " " + p;
        cmd += "\"";
    }
    cmd += " 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};

    char buf[4096] = {};
    bool got = (std::fgets(buf, sizeof(buf), pipe) != nullptr);
    pclose(pipe);
    if (!got) return {};

    std::string result(buf);
    while (!result.empty() &&
           (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    return result;
}

std::string pickFileOrDirectory(const std::string& title) {
    return pickFile(title, {}, "All files");
}

#endif  // !defined(__APPLE__)
