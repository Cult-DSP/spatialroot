Audit:
Dependency Audit — Cross-Platform Build
What's vendored/submoduled (good — no system install needed)
Dependency Location Status
AlloLib (DBAP/VBAP/LBAP + rtaudio/rtmidi/GLFW/glad/imgui/oscpack) thirdparty/allolib submodule, init.sh initializes ✅
libsndfile thirdparty/libsndfile submodule — never initialized by init.sh or init.ps1 ❌
libbw64 (cult_transcoder) cult_transcoder/thirdparty/libbw64 submodule, init.sh initializes ✅
pugixml FetchContent at configure-time fetched from GitHub, needs internet ⚠️
Catch2 FetchContent at configure-time fetched from GitHub, needs internet ⚠️
Critical Bug — thirdparty/libsndfile is never initialized
CMakeLists.txt:32-35 calls add_subdirectory(thirdparty/libsndfile) unconditionally, but neither init.sh nor init.ps1 ever initialize that submodule. On a fresh clone the directory is empty and CMake will immediately fail. macOS currently "works" only because the developer's local clone already has it checked out from a prior manual git submodule update --init --recursive.

System Dependencies by Platform
macOS ✅ (works — all provided by Xcode CLT + system frameworks)
Xcode Command Line Tools — provides clang++, make, libtool
CoreAudio, CoreFoundation — rtaudio (rtaudio/CMakeLists.txt)
CoreMIDI, CoreServices — rtmidi
OpenGL framework — AlloLib: find_package(OpenGL REQUIRED) (allolib/CMakeLists.txt:55)
No extra brew installs required for non-GUI build
Linux ⚠️ (CI passes, but init.sh gives no guidance to users)
Required packages (all in CI ci.yml:29-39):

Package Why required Hard failure if missing?
build-essential gcc/g++/make yes
libasound2-dev ALSA — RTAUDIO_API_ALSA ON by default on Linux, rtaudio emits FATAL_ERROR if headers missing yes
libpulse-dev PulseAudio — rtaudio also auto-enables if pkg-config finds it soft (skipped if absent)
libgl1-mesa-dev + libglu1-mesa-dev OpenGL headers — AlloLib find_package(OpenGL REQUIRED) yes
libx11-dev, libxrandr-dev, libxi-dev, libxinerama-dev, libxcursor-dev GLFW X11 backend — AlloLib always builds its embedded GLFW, even with SPATIALROOT_BUILD_GUI=OFF yes
pkg-config rtaudio/rtmidi use it for ALSA/JACK/Pulse detection usually pre-installed, but not guaranteed
init.sh has no Linux prerequisite check or install hint beyond "cmake not found."

Windows ❌ (no CI, build never tested)
Required toolchain (none checked by init.ps1):

Visual Studio 2019+ with "Desktop development with C++" workload
Provides MSVC compiler + Windows SDK (covers ksuser, mfplat, mfuuid, wmcodecdspuuid, winmm, ole32, Ws2_32, setupapi — all linked by rtaudio/rtmidi/oscpack)
CMake 3.20+ (checked ✅)
Git (checked ✅)
thirdparty/libsndfile not initialized — same critical gap as above
Additional Windows-specific issues:

build.ps1 uses cmake --build ... --config Release (multi-config generator assumed), but the configure step doesn't specify -G — if cmake resolves to a Ninja or MinGW generator the --config flag is silently ignored and a Debug build is produced
No Windows runner in CI — breakage won't be caught automatically
Other Issues
Stale submodule entries in .gitmodules: thirdparty/libbw64 and thirdparty/libadm are registered at the spatialroot level but are not referenced by any active CMakeLists.txt (the spatialroot_adm_extract tool was archived in Phase 3). These entries persist and could be confusing or cause issues with git submodule update --init --recursive.

FetchContent requires network at configure time: cult_transcoder/CMakeLists.txt fetches Catch2 and pugixml from GitHub every first configure. In CI the network is available, but this is a potential problem for air-gapped builds or build caching strategies.

Summary Table
Platform Blocker Needs manual work
macOS libsndfile submodule not initialized by init.sh Yes
Linux Same libsndfile gap + init.sh has no apt prereq hint Yes
Windows libsndfile not initialized + no CI coverage + potential generator mismatch Yes
The single highest-leverage fix across all three platforms is adding libsndfile initialization to both init.sh and init.ps1. Everything else is either documentation (Linux prereq hints, Windows toolchain requirements) or lower-risk CI infrastructure.
