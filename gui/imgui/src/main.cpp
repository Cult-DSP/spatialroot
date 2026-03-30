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

    ImGui::StyleColorsDark();

    // Slightly soften the default dark theme
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 4.f;
    style.FrameRounding     = 3.f;
    style.GrabRounding      = 3.f;
    style.ItemSpacing       = ImVec2(8.f, 5.f);
    style.FramePadding      = ImVec2(6.f, 4.f);
    style.Colors[ImGuiCol_WindowBg]  = ImVec4(0.08f, 0.08f, 0.08f, 1.f);
    style.Colors[ImGuiCol_ChildBg]   = ImVec4(0.11f, 0.11f, 0.11f, 1.f);
    style.Colors[ImGuiCol_FrameBg]   = ImVec4(0.16f, 0.16f, 0.17f, 1.f);
    style.Colors[ImGuiCol_Header]    = ImVec4(0.20f, 0.20f, 0.22f, 1.f);
    style.Colors[ImGuiCol_Button]    = ImVec4(0.22f, 0.22f, 0.24f, 1.f);

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
        glClearColor(0.08f, 0.08f, 0.08f, 1.f);
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
