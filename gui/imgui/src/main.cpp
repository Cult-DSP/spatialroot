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
//   ./spatialroot_gui                   # project root = "." (run from repo root)
//   ./spatialroot_gui --root /path/to/spatialroot
//   ./spatialroot_gui --help

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include "App.hpp"

#include <cstdio>
#include <cstring>
#include <string>

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
    printf("Usage: %s [--root <project_root>] [--help]\n", prog);
    printf("\n");
    printf("  --root <path>   Path to the spatialroot project root.\n");
    printf("                  Defaults to '.' (current directory).\n");
    printf("                  Run from the project root for layouts and\n");
    printf("                  cult-transcoder to resolve correctly.\n");
    printf("  --help          Show this message.\n");
    printf("\n");
}

static std::string parseProjectRoot(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            exit(0);
        }
        if ((strcmp(argv[i], "--root") == 0) && (i + 1 < argc)) {
            return std::string(argv[i + 1]);
        }
    }
    return ".";
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    std::string projectRoot = parseProjectRoot(argc, argv);

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        fprintf(stderr, "[main] glfwInit() failed\n");
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
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // vsync

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

    // ── App setup ────────────────────────────────────────────────────────────
    App app(projectRoot);

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
