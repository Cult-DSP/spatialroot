---
name: spatialroot GUI framework decision
description: GUI framework chosen for Stage 3 replacement of Python PySide6 GUI
type: project
---

GUI framework: Dear ImGui (MIT, ~5MB submodule) + GLFW (zlib, ~1MB submodule). NOT Qt.

Qt was ruled out because it cannot be used as a git submodule — the source is multi-GB with a complex bootstrap build. All project dependencies must be open-source git submodules.

Stage 3 GUI lives at gui/imgui/ (not gui/qt/ as original plan said). The SPATIALROOT_BUILD_GUI option in root CMakeLists.txt should point at gui/imgui/ when Stage 3 begins.

**Why:** User constraint: everything must be an open-source submodule, no brew/system installs.

**How to apply:** When Stage 3 starts, add Dear ImGui and GLFW as submodules, create gui/imgui/, update root CMakeLists.txt SPATIALROOT_BUILD_GUI target. All architectural requirements (link EngineSessionCore, staged lifecycle, runtime setters, update() polling loop, no OSC dependency) are unchanged from the original Qt plan.
