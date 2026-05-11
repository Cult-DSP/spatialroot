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
#include <shlobj.h>

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
    // Use IFileOpenDialog with FOS_PICKFOLDERS | FOS_ALLFILESYSTEMED to allow both files and folders.
    // Double-clicking a folder navigates into it; selecting and clicking Open returns the path.
    IFileOpenDialog* pDialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDialog));
    if (FAILED(hr)) return {};

    wchar_t titleW[MAX_PATH] = {};
    mbstowcs_s(nullptr, titleW, title.c_str(), MAX_PATH - 1);
    pDialog->SetTitle(titleW);
    DWORD opts = 0;
    pDialog->GetOptions(&opts);
    pDialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_ALLFILESYSTEMED);

    if (pDialog->Show(nullptr) == S_OK) {
        IShellItem* pItem = nullptr;
        if (pDialog->GetResult(&pItem) == S_OK) {
            PWSTR pszPath = nullptr;
            if (pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath) == S_OK) {
                char pathA[MAX_PATH] = {};
                wcstombs_s(nullptr, pathA, pszPath, MAX_PATH - 1);
                std::string result(pathA);
                CoTaskMemFree(pszPath);
                pItem->Release();
                pDialog->Release();
                return result;
            }
            pItem->Release();
        }
    }
    pDialog->Release();
    return {};
}

std::string pickDirectory(const std::string& title) {
    BROWSEINFOA bi = {};
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return {};

    char path[MAX_PATH] = {};
    const bool ok = SHGetPathFromIDListA(pidl, path);
    CoTaskMemFree(pidl);
    return ok ? std::string(path) : std::string();
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
    // zenity --file-selection (without --directory) allows both files and folders.
    // Double-clicking a folder navigates into it.
    std::string cmd = "zenity --file-selection --title=\"" + title + "\" 2>/dev/null";
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

std::string pickDirectory(const std::string& title) {
    std::string cmd = "zenity --file-selection --directory --title=\"" + title + "\" 2>/dev/null";
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

#endif  // !defined(__APPLE__)
