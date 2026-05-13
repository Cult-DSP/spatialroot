// main.cpp — Spatial Root Dear ImGui + GLFW desktop GUI
//
// Sets up a GLFW window with an OpenGL 3.3 core profile context, initialises
// Dear ImGui, and drives the App render loop.
//
// Platform notes:
//   macOS  — OpenGL 3.3 core profile, GLFW_OPENGL_FORWARD_COMPAT required.
//            GL is deprecated on macOS but fully functional. GL_SILENCE_DEPRECATION
//            is defined in CMakeLists.txt to suppress the build warnings.
//   Windows — Links opengl32 via OpenGL::GL (CMake). No extra config needed.
//   Linux   — Links -lGL. Requires a desktop OpenGL 3.3 driver.
//
// Usage:
//   ./spatialroot_gui                   # package-relative lookup, repo-root fallback
//   ./spatialroot_gui --root /path/to/spatialroot
//   ./spatialroot_gui --help

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include "App.hpp"
#include "StartupLogger.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

#ifdef __APPLE__
// Defined in FileDialog_macOS.mm — sets the Dock/app-switcher icon from embedded bytes.
void setMacOSAppIconFromData(const unsigned char* data, unsigned int len);
// miniLogo_data.h is included (and thus defined) in App.cpp only.
// Declare the symbols extern here to avoid duplicate definitions at link time.
extern unsigned char miniLogo_png[];
extern unsigned int  miniLogo_png_len;
#endif

// ── GLFW error callback ───────────────────────────────────────────────────────
static void glfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "[GLFW] Error %d: %s\n", error, description);
}

// ── Argument parsing ──────────────────────────────────────────────────────────
static void printUsage(const char* prog) {
    printf("Usage: %s [--root <project_root>] [--keep-temp-sessions] [--temp-root <path>] [--package-self-test] [--help]\n", prog);
    printf("\n");
    printf("  --root <path>   Developer override for the spatialroot project root.\n");
    printf("                  Defaults to '.' and is only used after packaged\n");
    printf("                  bundle/install-tree lookups are exhausted.\n");
    printf("  --keep-temp-sessions\n");
    printf("                  Preserve generated temp sessions for debugging.\n");
    printf("                  By default they are deleted on app close.\n");
    printf("  --temp-root <path>\n");
    printf("                  Developer override for the temp session root.\n");
    printf("  --package-self-test\n");
    printf("                  Verify packaged paths, helper binaries, resources,\n");
    printf("                  temp/cache writability, and Windows runtime DLLs.\n");
    printf("  --help          Show this message.\n");
    printf("\n");
}

struct AppLaunchOptions {
    std::string projectRoot = ".";
    bool keepTempSessions = false;
    std::string tempRootOverride;
    bool packageSelfTest = false;
};

static AppLaunchOptions parseLaunchOptions(int argc, char* argv[]) {
    AppLaunchOptions opts;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            exit(0);
        }
        if ((strcmp(argv[i], "--root") == 0) && (i + 1 < argc)) {
            opts.projectRoot = argv[++i];
        } else if (strcmp(argv[i], "--keep-temp-sessions") == 0) {
            opts.keepTempSessions = true;
        } else if ((strcmp(argv[i], "--temp-root") == 0) && (i + 1 < argc)) {
            opts.tempRootOverride = argv[++i];
        } else if (strcmp(argv[i], "--package-self-test") == 0) {
            opts.packageSelfTest = true;
        }
    }
    return opts;
}

static fs::path currentExecutablePath() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len = GetModuleFileNameW(nullptr, buffer.data(),
                                             static_cast<DWORD>(buffer.size()));
        if (len == 0) return {};
        if (len < buffer.size() - 1) {
            buffer.resize(len);
            return fs::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 1024;
    std::vector<char> buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        buffer.assign(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    }
    return fs::weakly_canonical(fs::path(buffer.data()));
#else
    std::vector<char> buffer(1024, '\0');
    for (;;) {
        const ssize_t len = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (len < 0) return {};
        if (static_cast<size_t>(len) < buffer.size() - 1) {
            buffer[static_cast<size_t>(len)] = '\0';
            return fs::path(buffer.data());
        }
        buffer.resize(buffer.size() * 2, '\0');
    }
#endif
}

static fs::path executableDirectory() {
    const fs::path exe = currentExecutablePath();
    return exe.empty() ? fs::path{} : exe.parent_path();
}

static fs::path macBundleResourcesDirectory() {
#ifdef __APPLE__
    const fs::path exeDir = executableDirectory();
    if (exeDir.empty()) return {};
    const fs::path contentsDir = exeDir.parent_path();
    if (contentsDir.filename() != "Contents") return {};
    const fs::path resourcesDir = contentsDir / "Resources";
    std::error_code ec;
    if (fs::exists(resourcesDir, ec)) return resourcesDir;
#endif
    return {};
}

static fs::path installPrefixFromExecutable() {
    const fs::path exeDir = executableDirectory();
    if (exeDir.empty() || exeDir.filename() != "bin") return {};
    return exeDir.parent_path();
}

static std::string withExecutableSuffix(std::string name) {
#ifdef _WIN32
    name += ".exe";
#endif
    return name;
}

static void appendCandidate(std::vector<fs::path>& candidates, const fs::path& path) {
    if (path.empty()) return;
    if (std::find(candidates.begin(), candidates.end(), path) == candidates.end()) {
        candidates.push_back(path);
    }
}

static fs::path resolveAppRoot() {
    if (const fs::path resourcesDir = macBundleResourcesDirectory(); !resourcesDir.empty()) {
        return resourcesDir.parent_path().parent_path();
    }
    if (const fs::path installPrefix = installPrefixFromExecutable(); !installPrefix.empty()) {
        return installPrefix;
    }
    return executableDirectory();
}

static fs::path resolveResourcesPath() {
    if (const char* assetRoot = std::getenv("SPATIALROOT_ASSET_ROOT"); assetRoot && *assetRoot) {
        return fs::path(assetRoot);
    }
    if (const fs::path resourcesDir = macBundleResourcesDirectory(); !resourcesDir.empty()) {
        return resourcesDir;
    }
    if (const fs::path installPrefix = installPrefixFromExecutable(); !installPrefix.empty()) {
        return installPrefix / "share" / "spatialroot";
    }
    if (const fs::path exeDir = executableDirectory(); !exeDir.empty()) {
        return exeDir / "resources";
    }
    return {};
}

static fs::path resolveLayoutsPath(const std::string& projectRoot) {
    std::vector<fs::path> candidates;
    const fs::path resourcesPath = resolveResourcesPath();
    if (!resourcesPath.empty()) {
        appendCandidate(candidates, resourcesPath / "speaker_layouts");
        appendCandidate(candidates, resourcesPath / "source" / "speaker_layouts");
    }
    if (const fs::path exeDir = executableDirectory(); !exeDir.empty()) {
        appendCandidate(candidates, exeDir / "speaker_layouts");
        appendCandidate(candidates, exeDir.parent_path() / "share" / "spatialroot" / "speaker_layouts");
    }
    appendCandidate(candidates, fs::path(projectRoot) / "source" / "speaker_layouts");

    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate, ec)) return candidate;
        ec.clear();
    }
    return candidates.empty() ? fs::path{} : candidates.front();
}

static fs::path findCultTranscoder(const std::string& projectRoot) {
    if (const char* env = std::getenv("SPATIALROOT_CULT_TRANSCODER"); env && *env) {
        return fs::path(env);
    }

    std::vector<fs::path> candidates;
    if (const fs::path exeDir = executableDirectory(); !exeDir.empty()) {
        appendCandidate(candidates, exeDir / withExecutableSuffix("cult-transcoder"));
        appendCandidate(candidates, exeDir.parent_path() / "libexec" / "spatialroot" /
                                    withExecutableSuffix("cult-transcoder"));
    }
    if (const fs::path resourcesDir = macBundleResourcesDirectory(); !resourcesDir.empty()) {
        appendCandidate(candidates, resourcesDir / "bin" / withExecutableSuffix("cult-transcoder"));
    }
    appendCandidate(candidates, fs::path(projectRoot) / "build" / "internal" / "cult_transcoder" /
                                withExecutableSuffix("cult-transcoder"));
    appendCandidate(candidates, fs::path(projectRoot) / "internal" / "cult_transcoder" / "build" /
                                withExecutableSuffix("cult-transcoder"));

    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate, ec)) return candidate;
        ec.clear();
    }
    return candidates.empty() ? fs::path{} : candidates.front();
}

static fs::path findSpatialRenderer(const std::string& projectRoot) {
    if (const char* env = std::getenv("SPATIALROOT_SPATIAL_RENDER"); env && *env) {
        return fs::path(env);
    }

    std::vector<fs::path> candidates;
    if (const fs::path exeDir = executableDirectory(); !exeDir.empty()) {
        appendCandidate(candidates, exeDir / withExecutableSuffix("spatialroot_spatial_render"));
        appendCandidate(candidates, exeDir.parent_path() / "bin" /
                                    withExecutableSuffix("spatialroot_spatial_render"));
    }
    appendCandidate(candidates, fs::path(projectRoot) / "build" / "source" / "spatial_engine" /
                                "spatialRender" / withExecutableSuffix("spatialroot_spatial_render"));

    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate, ec)) return candidate;
        ec.clear();
    }
    return candidates.empty() ? fs::path{} : candidates.front();
}

static bool canWriteTestFile(const fs::path& root, fs::path& testFileOut, std::string& errorOut) {
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        errorOut = ec.message();
        return false;
    }

    testFileOut = root / "SpatialRoot-package-self-test.tmp";
    std::ofstream out(testFileOut, std::ios::trunc);
    if (!out) {
        errorOut = "failed to open file for writing";
        return false;
    }
    out << "Spatial Root package self-test\n";
    out.close();

    fs::remove(testFileOut, ec);
    if (ec) errorOut = "write succeeded, cleanup failed: " + ec.message();
    return true;
}

static int runPackageSelfTest(const AppLaunchOptions& launchOptions) {
    const fs::path exePath = currentExecutablePath();
    const fs::path exeDir = executableDirectory();
    const fs::path appRoot = resolveAppRoot();
    const fs::path resourcesPath = resolveResourcesPath();
    const fs::path layoutsPath = resolveLayoutsPath(launchOptions.projectRoot);
    const fs::path cultPath = findCultTranscoder(launchOptions.projectRoot);
    const fs::path rendererPath = findSpatialRenderer(launchOptions.projectRoot);
    const fs::path cacheRoot = SpatialRootPaths::cacheRoot(launchOptions.tempRootOverride);

    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);

    fs::path tempTestFile;
    std::string tempWriteError;
    const bool tempWritable = canWriteTestFile(cacheRoot, tempTestFile, tempWriteError);

    std::ostringstream report;
    report << "Spatial Root package self-test\n";
    report << "executable path: " << (exePath.empty() ? "<unresolved>" : exePath.string()) << '\n';
    report << "app root: " << (appRoot.empty() ? "<unresolved>" : appRoot.string()) << '\n';
    report << "resources path: " << (resourcesPath.empty() ? "<unresolved>" : resourcesPath.string()) << '\n';
    report << "resources exist: " << (fs::exists(resourcesPath) ? "yes" : "no") << '\n';
    report << "layouts path: " << (layoutsPath.empty() ? "<unresolved>" : layoutsPath.string()) << '\n';
    report << "layouts exist: " << (fs::exists(layoutsPath) ? "yes" : "no") << '\n';
    report << "cult-transcoder path: " << (cultPath.empty() ? "<unresolved>" : cultPath.string()) << '\n';
    report << "cult-transcoder exists: " << (fs::exists(cultPath) ? "yes" : "no") << '\n';
    report << "spatial-render/helper path: " << (rendererPath.empty() ? "<unresolved>" : rendererPath.string()) << '\n';
    report << "spatial-render/helper exists: " << (fs::exists(rendererPath) ? "yes" : "no") << '\n';
    report << "current working directory: " << (cwd.empty() ? "<unresolved>" : cwd.string()) << '\n';
    report << "writable temp/cache path: " << cacheRoot.string() << '\n';
    report << "temp/cache write test: " << (tempWritable ? "ok" : "failed") << '\n';
    if (!tempWritable && !tempWriteError.empty()) {
        report << "temp/cache write error: " << tempWriteError << '\n';
    }

#ifdef _WIN32
    const char* runtimeDlls[] = {
        "msvcp140.dll",
        "vcruntime140.dll",
        "vcruntime140_1.dll",
    };
    for (const char* dll : runtimeDlls) {
        report << "runtime dll " << dll << ": "
               << (fs::exists(exeDir / dll) ? "present" : "missing") << '\n';
    }
#endif

    const std::string reportText = report.str();
    printf("%s", reportText.c_str());

    fs::path logPath = appRoot / "SpatialRoot-package-self-test.log";
    if (appRoot.empty()) logPath = "SpatialRoot-package-self-test.log";
    std::ofstream logOut(logPath, std::ios::trunc);
    if (logOut) logOut << reportText;

    const bool ok = fs::exists(resourcesPath) &&
                    fs::exists(layoutsPath) &&
                    fs::exists(cultPath) &&
                    fs::exists(rendererPath) &&
                    tempWritable;
#ifdef _WIN32
    const bool runtimeOk = fs::exists(exeDir / "msvcp140.dll") &&
                           fs::exists(exeDir / "vcruntime140.dll") &&
                           fs::exists(exeDir / "vcruntime140_1.dll");
    return (ok && runtimeOk) ? 0 : 1;
#else
    return ok ? 0 : 1;
#endif
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    const AppLaunchOptions launchOptions = parseLaunchOptions(argc, argv);
    const fs::path exePath = currentExecutablePath();
    const fs::path appRoot = resolveAppRoot();
    const fs::path resourcesPath = resolveResourcesPath();
    const fs::path cultPath = findCultTranscoder(launchOptions.projectRoot);
    const fs::path rendererPath = findSpatialRenderer(launchOptions.projectRoot);

    StartupLogger::initialize(appRoot);
    StartupLogger::append("startup entered");
    StartupLogger::append("resolved executable path: " + (exePath.empty() ? std::string("<unresolved>") : exePath.string()));
    StartupLogger::append("resolved app root: " + (appRoot.empty() ? std::string("<unresolved>") : appRoot.string()));
    StartupLogger::append("resolved resources directory: " + (resourcesPath.empty() ? std::string("<unresolved>") : resourcesPath.string()));
    StartupLogger::append("helper binary check cult-transcoder: " + (cultPath.empty() ? std::string("<unresolved>") : cultPath.string()) +
                          " (exists=" + (fs::exists(cultPath) ? "yes" : "no") + ")");
    StartupLogger::append("helper binary check spatial-render: " + (rendererPath.empty() ? std::string("<unresolved>") : rendererPath.string()) +
                          " (exists=" + (fs::exists(rendererPath) ? "yes" : "no") + ")");

    if (launchOptions.packageSelfTest) {
        StartupLogger::append("package self-test requested");
        return runPackageSelfTest(launchOptions);
    }

    StartupLogger::append("GUI init start");
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        fprintf(stderr, "[main] glfwInit() failed\n");
        StartupLogger::append("last successful startup step before failure: GUI init start");
        return 1;
    }

#ifdef __APPLE__
    // Set the Dock / app-switcher icon from embedded bytes — no path lookup needed.
    setMacOSAppIconFromData(miniLogo_png, miniLogo_png_len);
#endif

    // OpenGL 3.3 Core Profile
    // macOS requires GLFW_OPENGL_FORWARD_COMPAT for core profile.
    const char* glsl_version = "#version 330 core";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(
        860, 920, "Spatial Root — Real-Time Engine", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "[main] glfwCreateWindow() failed\n");
        glfwTerminate();
        StartupLogger::append("last successful startup step before failure: glfwInit");
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // vsync
    StartupLogger::append("window + OpenGL context ready");

    // ── Dear ImGui setup ─────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // disable imgui.ini persistence (dev tool)

    // ── Font: Menlo (macOS system monospace). Falls back to ImGui built-in. ──────
    // Menlo ships with every macOS since 10.6 and matches the reference aesthetic.
    {
        const char* menloPath = "/System/Library/Fonts/Menlo.ttc";
        FILE* f = fopen(menloPath, "rb");
        if (f) {
            fclose(f);
            io.Fonts->AddFontFromFileTTF(menloPath, 13.5f);
        }
        // If Menlo is not found (non-macOS), the default ImGui bitmap font is used.
    }

    // ── Theme: dark background, warm cream cards (matches reference prototype) ──
    ImGui::StyleColorsLight();  // start from light base, then override

    ImGuiStyle& style = ImGui::GetStyle();

    // Shape
    style.WindowRounding    = 0.f;
    style.ChildRounding     = 4.f;
    style.FrameRounding     = 3.f;
    style.GrabRounding      = 10.f;   // circular slider grab
    style.PopupRounding     = 4.f;
    style.ScrollbarRounding = 3.f;
    style.TabRounding       = 3.f;
    style.WindowBorderSize  = 0.f;
    style.ChildBorderSize   = 1.f;
    style.FrameBorderSize   = 1.f;
    style.ItemSpacing       = ImVec2(8.f, 6.f);
    style.ItemInnerSpacing  = ImVec2(6.f, 4.f);
    style.FramePadding      = ImVec2(7.f, 4.f);
    style.WindowPadding     = ImVec2(12.f, 10.f);
    style.ScrollbarSize     = 10.f;
    style.GrabMinSize       = 10.f;

    // Palette
    auto& C = style.Colors;
    // Outer window (the dark desktop surround)
    C[ImGuiCol_WindowBg]          = ImVec4(0.07f, 0.07f, 0.07f, 1.f);
    // Cards / child windows
    C[ImGuiCol_ChildBg]           = ImVec4(0.88f, 0.86f, 0.82f, 1.f);
    C[ImGuiCol_Border]            = ImVec4(0.65f, 0.62f, 0.57f, 1.f);
    C[ImGuiCol_BorderShadow]      = ImVec4(0.f,   0.f,   0.f,   0.f);
    // Text
    C[ImGuiCol_Text]              = ImVec4(0.10f, 0.09f, 0.08f, 1.f);
    C[ImGuiCol_TextDisabled]      = ImVec4(0.42f, 0.40f, 0.36f, 1.f);
    // Input / frame
    C[ImGuiCol_FrameBg]           = ImVec4(0.93f, 0.91f, 0.87f, 1.f);
    C[ImGuiCol_FrameBgHovered]    = ImVec4(0.89f, 0.87f, 0.83f, 1.f);
    C[ImGuiCol_FrameBgActive]     = ImVec4(0.83f, 0.81f, 0.77f, 1.f);
    // Buttons
    C[ImGuiCol_Button]            = ImVec4(0.84f, 0.82f, 0.78f, 1.f);
    C[ImGuiCol_ButtonHovered]     = ImVec4(0.77f, 0.75f, 0.71f, 1.f);
    C[ImGuiCol_ButtonActive]      = ImVec4(0.68f, 0.66f, 0.62f, 1.f);
    // Headers (combo, collapsible, etc.)
    C[ImGuiCol_Header]            = ImVec4(0.80f, 0.78f, 0.74f, 1.f);
    C[ImGuiCol_HeaderHovered]     = ImVec4(0.74f, 0.72f, 0.68f, 1.f);
    C[ImGuiCol_HeaderActive]      = ImVec4(0.68f, 0.66f, 0.62f, 1.f);
    // Separator
    C[ImGuiCol_Separator]         = ImVec4(0.60f, 0.57f, 0.52f, 1.f);
    C[ImGuiCol_SeparatorHovered]  = ImVec4(0.50f, 0.47f, 0.43f, 1.f);
    C[ImGuiCol_SeparatorActive]   = ImVec4(0.40f, 0.38f, 0.34f, 1.f);
    // Sliders
    C[ImGuiCol_SliderGrab]        = ImVec4(0.18f, 0.17f, 0.15f, 1.f);
    C[ImGuiCol_SliderGrabActive]  = ImVec4(0.08f, 0.07f, 0.06f, 1.f);
    // Checkmark
    C[ImGuiCol_CheckMark]         = ImVec4(0.12f, 0.11f, 0.10f, 1.f);
    // Scrollbar
    C[ImGuiCol_ScrollbarBg]       = ImVec4(0.82f, 0.80f, 0.76f, 1.f);
    C[ImGuiCol_ScrollbarGrab]     = ImVec4(0.55f, 0.52f, 0.48f, 1.f);
    C[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.42f, 0.38f, 1.f);
    C[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.35f, 0.32f, 0.29f, 1.f);
    // Tabs
    C[ImGuiCol_Tab]               = ImVec4(0.78f, 0.76f, 0.72f, 1.f);
    C[ImGuiCol_TabHovered]        = ImVec4(0.88f, 0.86f, 0.82f, 1.f);
    C[ImGuiCol_TabSelected]       = ImVec4(0.88f, 0.86f, 0.82f, 1.f);
    C[ImGuiCol_TabSelectedOverline] = ImVec4(0.18f, 0.17f, 0.15f, 1.f);
    C[ImGuiCol_TabDimmed]         = ImVec4(0.72f, 0.70f, 0.66f, 1.f);
    C[ImGuiCol_TabDimmedSelected] = ImVec4(0.82f, 0.80f, 0.76f, 1.f);
    // Popup
    C[ImGuiCol_PopupBg]           = ImVec4(0.91f, 0.89f, 0.85f, 1.f);
    // Title bar (unused — root window has no title)
    C[ImGuiCol_TitleBg]           = ImVec4(0.78f, 0.76f, 0.72f, 1.f);
    C[ImGuiCol_TitleBgActive]     = ImVec4(0.72f, 0.70f, 0.66f, 1.f);
    // Clear color matches the outer window background
    // (set in the render loop: glClearColor)

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
    StartupLogger::append("ImGui init complete");

    // ── App setup ────────────────────────────────────────────────────────────
    App app(launchOptions.projectRoot,
            launchOptions.keepTempSessions,
            launchOptions.tempRootOverride);
    StartupLogger::append("startup complete; audio/backend init deferred until engine start");

    // Wire GLFW window close callback → App::requestShutdown()
    // IMPORTANT: Must call session.shutdown() before the process exits to avoid
    // deadlock on macOS CoreAudio (see api_mismatch_ledger.md constraint #4).
    glfwSetWindowUserPointer(window, &app);
    glfwSetWindowCloseCallback(window, [](GLFWwindow* w) {
        App* a = static_cast<App*>(glfwGetWindowUserPointer(w));
        a->requestShutdown();
        // GLFW will close the window after this callback returns.
    });

    // ── Render loop ──────────────────────────────────────────────────────────
    // glfwWaitEventsTimeout(0.05) sleeps up to 50 ms between frames when there
    // is no user input, keeping CPU load near zero while idle. This also serves
    // as the engine polling interval — update() and queryStatus() run at ~20 Hz
    // minimum, satisfying the "50 ms polling interval" contract from API.md.
    while (!glfwWindowShouldClose(window)) {
        glfwWaitEventsTimeout(0.05);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.tick();

        ImGui::Render();

        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.07f, 0.07f, 0.07f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // ── Teardown ─────────────────────────────────────────────────────────────
    // requestShutdown() may have already been called via the close callback,
    // but calling it again is safe (guarded internally).
    app.requestShutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
