Workflow file: .github/workflows/ci.yml

What build path was chosen
Direct CMake invocation — cmake -B build -DCMAKE_BUILD_TYPE=Release -DSPATIALROOT_BUILD_GUI=OFF then cmake --build build --parallel. This is exactly what build.sh does under the hood, just called directly so CI steps are explicit and debuggable without depending on the shell script. All three core targets build by default (ENGINE, OFFLINE, CULT); GUI is explicitly disabled because gui/imgui/ does not yet exist and the CMakeLists.txt will FATAL_ERROR if you enable it.

How submodules are handled
actions/checkout@v4 with submodules: recursive — this fetches all submodules in one step, including nested ones (cult_transcoder/thirdparty/libbw64, cult_transcoder/thirdparty/libadm, and AlloLib's own submodules). The thirdparty/imgui and thirdparty/glfw submodules will also be checked out (they're registered in .gitmodules) but have no effect since GUI is off.

Platforms covered
ubuntu-latest (Ubuntu 22.04, gcc, make)
macos-latest (macOS 14 Sonoma, Apple Clang, make)
fail-fast: false keeps both matrix legs running independently so you see failures on both platforms at once rather than stopping after the first.

Ubuntu system packages
AlloLib is a full multimedia library — even with examples/tests disabled, it compiles OpenGL and audio I/O code unconditionally. The package list covers:

build-essential — gcc, g++, make
libasound2-dev, libpulse-dev — ALSA/PulseAudio (RtAudio backend)
libgl1-mesa-dev, libglu1-mesa-dev — OpenGL headers (AlloLib links GL)
libx11-dev, libxrandr-dev, libxi-dev, libxinerama-dev, libxcursor-dev — X11 headers (GLFW and AlloLib window system)
macOS needs none of these — CoreAudio and OpenGL are system frameworks.

Branch
Triggers on push and PR to main, which matches the repo's current default branch.

Assumptions and limitations in v1
No audio device testing — realtime engine is built but not run; hardware audio I/O is untestable on headless CI runners.
GUI excluded — gui/imgui/ does not exist yet; enabling it would be a fatal CMake error.
No caching — dependency fetching (submodules, AlloLib build) happens fresh each run. Fine for v1; worth revisiting if build times become painful.
No artifact upload — binaries are discarded after the job. Build success/failure is the only signal needed for v1.
CMake version — ubuntu-latest and macos-latest runners ship CMake 3.25+, satisfying the 3.20 minimum.
Windows excluded — per scope; PowerShell scripts already exist if you want to add it later.
