#include "FileDialog.hpp"

#include <cstdio>

// macOS: implemented in FileDialog_macOS.mm (NSOpenPanel via Objective-C++).
// Windows and Linux implementations follow.

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shobjidl.h>

namespace {
std::wstring wideFromUtf8OrAnsi(const std::string& input) {
    if (input.empty()) return {};

    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), -1, nullptr, 0);
    if (len > 0) {
        std::wstring out(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), -1, out.data(), len);
        out.resize(static_cast<size_t>(len - 1));
        return out;
    }

    len = MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, out.data(), len);
    out.resize(static_cast<size_t>(len - 1));
    return out;
}

std::string narrowAnsiFromWide(const std::wstring& input) {
    if (input.empty()) return {};
    const int len = WideCharToMultiByte(CP_ACP, 0, input.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_ACP, 0, input.c_str(), -1, out.data(), len, nullptr, nullptr);
    out.resize(static_cast<size_t>(len - 1));
    return out;
}

class ScopedComInit {
public:
    ScopedComInit() {
        mHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    }
    ~ScopedComInit() {
        if (SUCCEEDED(mHr)) CoUninitialize();
    }
    bool ok() const { return SUCCEEDED(mHr) || mHr == RPC_E_CHANGED_MODE; }

private:
    HRESULT mHr = E_FAIL;
};

std::string runFileDialog(const std::wstring& title,
                          const std::vector<std::string>& filterPatterns,
                          const std::string& filterDescription,
                          bool pickFolders) {
    ScopedComInit com;
    if (!com.ok()) return {};

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) return {};

    dialog->SetTitle(title.c_str());

    DWORD opts = 0;
    dialog->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (pickFolders) opts |= FOS_PICKFOLDERS;
    dialog->SetOptions(opts);

    std::vector<std::wstring> widePatterns;
    std::vector<COMDLG_FILTERSPEC> specs;
    std::wstring descriptionWide = wideFromUtf8OrAnsi(filterDescription);
    if (!pickFolders && !filterPatterns.empty() && !descriptionWide.empty()) {
        std::wstring joined;
        widePatterns.reserve(filterPatterns.size());
        for (size_t i = 0; i < filterPatterns.size(); ++i) {
            widePatterns.push_back(wideFromUtf8OrAnsi(filterPatterns[i]));
            if (i > 0) joined += L';';
            joined += widePatterns.back();
        }
        widePatterns.push_back(joined);
        specs.push_back(COMDLG_FILTERSPEC{descriptionWide.c_str(), widePatterns.back().c_str()});
        dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        dialog->SetFileTypeIndex(1);
    }

    std::string result;
    if (dialog->Show(nullptr) == S_OK) {
        IShellItem* item = nullptr;
        if (dialog->GetResult(&item) == S_OK) {
            PWSTR path = nullptr;
            if (item->GetDisplayName(SIGDN_FILESYSPATH, &path) == S_OK) {
                result = narrowAnsiFromWide(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }

    dialog->Release();
    return result;
}
}

std::string pickFile(const std::string& title,
                     const std::vector<std::string>& filterPatterns,
                     const std::string& filterDescription) {
    return runFileDialog(wideFromUtf8OrAnsi(title), filterPatterns, filterDescription, false);
}

std::string pickFileOrDirectory(const std::string& title) {
    // Windows has no simple standard native dialog that truly allows both a
    // file and a folder selection in one picker without custom UI. Prefer the
    // explicit file/package buttons in the app; this fallback behaves as a
    // file picker to avoid surprising folder-only behavior.
    return runFileDialog(wideFromUtf8OrAnsi(title), {}, "", false);
}

std::string pickDirectory(const std::string& title) {
    return runFileDialog(wideFromUtf8OrAnsi(title), {}, "", true);
}

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
