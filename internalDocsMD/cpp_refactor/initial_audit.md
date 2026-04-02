1. Executive Verdict
   The repo is meaningfully closer to the paper's architecture than it first appears, but a persistent Python substrate obscures that fact. The realtime engine (EngineSession + its subsystems) is a real, well-implemented C++ library with a clean typed API. CULT is a working C++ CLI. The JSON-based LUSID contract is honored end-to-end in C++. What remains Python is largely launch infrastructure and GUI glue, not core audio logic.

The main structural gap between the repo and the paper is that the three surfaces (library API, CLI, GUI) are not cleanly separated at the process boundary: the GUI launches Python which launches the binary, the build system lives in Python, and there is no top-level CMake that ties the stack together. The C++ library API (EngineSession) is real but is not yet usable by an embedding host without pulling in the full engine directory.

The stack is not far from the paper's target — but the build and launch infrastructure needs to move out of Python before the architecture is legible.

2. Current Stack Map
   Realtime Engine — spatial_engine/realtimeEngine/
   Attribute Value
   Role Real-time spatial audio playback
   Language C++17
   State Production-ready, well-documented
   Runtime-critical Yes
   Key files EngineSession.hpp/.cpp, RealtimeBackend.hpp, RealtimeTypes.hpp, Streaming.hpp, Pose.hpp, Spatializer.hpp, OutputRemap.hpp, MultichannelReader.hpp
   Dependencies AlloLib (DBAP, audio I/O, OSC parameter server), shared loaders from spatial_engine/src/
   EngineSession implements the exact lifecycle described in API.md: configureEngine() → loadScene() → applyLayout() → configureRuntime() → start() → polling loop → shutdown(). The CMake builds both EngineSessionCore (a linkable static library) and spatialroot_realtime (the CLI binary). OSC parameter control via al::ParameterServer is wired in EngineSession.cpp. This is the most mature component.

Offline Renderer — spatial_engine/src/ and spatial_engine/spatialRender/
Attribute Value
Role Batch multichannel WAV rendering
Language C++17
State Functional, but secondary to realtime path
Runtime-critical No (offline only)
Key files SpatialRenderer.cpp/.hpp, VBAPRenderer.cpp/.hpp, JSONLoader.cpp/.hpp, LayoutLoader.cpp/.hpp, WavUtils.cpp/.hpp
JSONLoader and LayoutLoader are shared with the realtime engine (included directly via CMake target_include_directories). This is confirmed by the realtime engine's CMakeLists.txt adding ../src and ../src/renderer to its include path. SpatialRenderer itself is not shared — the realtime engine re-implements rendering in Spatializer.hpp.

CULT Transcoder — cult_transcoder/
Attribute Value
Role ADM→LUSID scene conversion (CLI + library header)
Language C++17
State Functional; ADM+Sony360RA working; MPEG-H incomplete
Runtime-critical Yes (required before realtime launch from ADM)
Key files include/cult_transcoder.hpp, include/adm_to_lusid.hpp, src/transcoder.cpp, src/adm_to_lusid.cpp, transcoding/adm/adm_reader.cpp
Dependencies pugixml (FetchContent), libbw64 (git submodule inside cult_transcoder/thirdparty/)
CULT has a genuine public API header (cult_transcoder.hpp with cult::transcode()) but CMake only builds the CLI binary, not a separately installable library target. The spatial engine does not call CULT as a C++ library — they communicate via subprocess from Python (runRealtime.py) or via shell.

Python Launch Infrastructure
File Role Status
runRealtime.py Top-level launcher for realtime path Active; calls setupCppTools() then invokes cult-transcoder + spatialroot_realtime as subprocesses
realtimeMain.py Alternate CLI entry point (referenced in README) Active (existence confirmed; README references it)
runPipeline.py Offline pipeline orchestrator Explicitly deprecated, kept for reference
src/config/configCPP_posix.py CMake/make orchestration Active; runs cmake and make via subprocess
activate.sh, engine.sh, init.sh Shell wrappers Active; engine.sh does a clean rebuild of realtime engine
Python GUI — gui/realtimeGUI/
Attribute Value
Role PySide6 desktop GUI for realtime control
Language Python 3 + PySide6
State Active, Phase 10 complete
Key pattern QProcess → runRealtime.py → subprocess → spatialroot_realtime; OSC via pythonosc.udp_client → al::ParameterServer
Dependencies PySide6, python-osc
The GUI is three process-hops from the audio thread: GUI → Python QProcess → runRealtime.py subprocess → C++ binary. OSC messages travel GUI → python-osc UDP → C++ al::ParameterServer. This works but means Python must be installed and the venv active for the GUI to function.

Python LUSID Library — LUSID/src/
File Role Status
scene.py Python dataclasses for LUSID scene nodes Active for offline pipeline only
xml_etree_parser.py Python ADM XML → LUSID (stdlib only) Superseded by CULT for realtime path; still used by deprecated offline pipeline
parser.py Python LUSID JSON loader Offline pipeline only
This library is not used by any C++ code. The C++ engine reads LUSID JSON via JSONLoader.cpp. CULT writes LUSID JSON via its own C++ implementation. The Python LUSID library is a parallel implementation that must stay in semantic sync with JSONLoader.cpp — a maintenance liability.

Python Build/Analysis Scripts — src/
File Role Status
src/config/configCPP\*.py cmake/make orchestration Active but should be shell scripts
src/analyzeADM/ ADM XML analysis Superseded by CULT for realtime; still used in deprecated runPipeline.py
src/packageADM/ Stem splitting, scene packaging Offline pipeline only, deprecated path
src/analyzeRender.py PDF render analysis Offline only
Virtual Environment — spatialroot/
A full Python 3.14 venv committed into the repo root. Contains jupyter, numpy, matplotlib, pandas, fonttools, scipy equivalents, PySide6, and python-osc. This is 1.6 GB by the audit report's count. For the realtime path, only PySide6 and python-osc are actually needed.

3. Paper Alignment Audit
   CULT as embeddable C++ transcoder with library API and CLI
   Partially aligned.

The CLI (cult-transcoder) is real and functional. The library header (cult_transcoder.hpp) exists and defines cult::transcode(). But CMake does not produce a separate static or shared library target — only the CLI binary. The spatial engine and the GUI reach CULT exclusively via subprocess, never via the C++ API. The paper's claim of "C++ library" is aspirational in the current build system.

LUSID as canonical scene and package structure
Aligned in spirit, partially in practice.

The JSON format is genuinely canonical: CULT writes it, JSONLoader.cpp reads it, the engine runs from it. The paper's claim holds for the realtime path. The gap is that there is no standalone C++ LUSID library — JSONLoader.cpp is the only C++ reader and it lives inside the engine's source tree, not as an exportable library. The Python LUSID library is a parallel reimplementation no longer needed for the primary (realtime) path.

Spatial Root as C++ runtime with library API, CLI, and optional GUI
Mostly aligned.

EngineSession is a real C++ library (EngineSessionCore target in CMake) with a clean typed API documented in API.md. The CLI binary (spatialroot_realtime) exists and works. The GUI exists. The disconnect is:

No CMake install target for EngineSessionCore — an embedding host cannot find it without building from the full source tree.
EngineOptions.elevationMode is int in the API, not the ElevationMode enum defined in RealtimeTypes.hpp — a minor type-safety gap noted in API.md.
Per-parameter runtime setters are listed as out-of-scope for V1 — the only runtime control path from C++ code is via OSC. A host embedding EngineSession must use OSC to change gain/focus at runtime, which is odd for a library consumer.
Consistent runtime control surfaces via OSC
Partially aligned.

al::ParameterServer provides a working OSC control surface. CLI args set initial values. The GUI sends OSC via Python. But the three surfaces are not consistent in shape: CLI uses positional/flag args with different names than OSC addresses, and the C++ API (configureRuntime()) is only available before start() — there are no setGain() / setFocus() methods callable while running from C++. The paper claims consistent control "across these surfaces" but currently they have different capabilities and reach.

Distinct but interoperable layers (CULT, LUSID, Spatial Root, Spatial Seed)
Partially aligned.

CULT and Spatial Root are distinct in the codebase. LUSID is a format, not a library. Spatial Seed is not present in this repo. The interoperability is real (CULT → LUSID JSON → engine), but it passes through Python subprocess calls rather than direct library linkage. The layers are interoperable at the file format level, not at the API level.

4. Python Dependency Audit
   gui/realtimeGUI/ (PySide6 GUI)
   Keep for now.

A native C++ GUI (Qt/C++ or ImGui) is a significant undertaking. The GUI works and provides real value. The OSC control surface is reasonable. The subprocess chain is ugly but not blocking. The only architectural pressure to change it would be if the GUI needs to embed the engine in-process (e.g., for tight visual feedback or no-subprocess deployment). That is not currently needed.

runRealtime.py (realtime launcher)
Migrate later — medium priority.

This is a thin launcher: it checks the .init_complete flag, calls setupCppTools(), invokes cult-transcoder as a subprocess, then invokes spatialroot_realtime as a subprocess. The audio work is 100% in the C++ binaries. The Python here is build-system scaffolding and argument forwarding. A shell script could do most of what this does, and the GUI could eventually invoke the C++ binary directly. The build-check responsibility belongs in a shell setup script, not in the runtime launcher.

What would replacement require: shell script for ADM path (invoke cult-transcoder then spatialroot_realtime), direct binary launch from GUI.

src/config/configCPP\*.py (CMake/make orchestration)
Migrate soon — low effort, high clarity.

This is build system logic implemented in Python. It calls subprocess.run(["cmake", ...]) and subprocess.run(["make", ...]). It should be a shell script (build.sh) or absorbed into a top-level CMakeLists.txt. Having the build system in Python means you need the venv active to build — that's an inverted dependency. This is one of the clearest architectural liabilities.

LUSID/src/ (Python LUSID library)
Migrate later — not blocking for realtime path.

The Python LUSID library is not used by any C++ code. For the realtime path it is dead weight. It remains because:

The deprecated offline pipeline (runPipeline.py) still imports it.
The LUSID unit tests (106 tests) are Python.
Once runPipeline.py is formally retired and CULT's C++ tests are expanded to cover the same ground, this library can be removed. There is no need to port it to C++ — CULT already has the C++ implementation, and JSONLoader.cpp has the C++ reader.

src/analyzeADM/ and src/packageADM/
Remove — tied to deprecated offline path only.

checkAudioChannels.py, extractMetadata.py, splitStems.py, packageForRender.py — these are all offline pipeline components. CULT superseded the ADM extraction and scene generation. Stem splitting was never used by the realtime path. The real value (offline batch rendering) is now served by cult-transcoder + spatialroot_spatial_render directly. These can be removed when runPipeline.py is formally retired.

src/analyzeRender.py (PDF analysis)
Keep for now — offline utility, no architectural pressure.

Produces PDF render analysis. Not runtime-critical. Low migration priority.

requirements.txt (numpy, pandas, jupyter, matplotlib, soundfile, gdown)
Reduce significantly — most not needed for realtime path.

For the realtime path + GUI: only PySide6 and python-osc are needed. The rest (numpy, soundfile, matplotlib, pandas, jupyter, gdown) are offline analysis and data utilities. They should be split into a requirements-dev.txt or removed entirely if the offline pipeline is retired.

5. Public API / CLI / GUI Reality Check
   C++ Library API
   EngineSession exists and is real. EngineSession.hpp defines all types. EngineSessionCore is a CMake static library target. API.md documents it with a working quick-start example.

What is missing for it to be genuinely embeddable:

No CMake install target (install(TARGETS EngineSessionCore DESTINATION lib) is not present)
No pkg-config or CMake find-package support
The API header depends on AlloLib headers at include time — an embedding host must also add AlloLib to its include path, which requires knowing AlloLib's directory layout
No per-parameter setters callable while running (setGain(float), setFocus(float)) — OSC is the only in-process runtime control path for a C++ host
The api_mismatch_ledger.md is honest about current constraints (staged setup is non-negotiable, OSC ownership is internal, shutdown sequence is mandatory). These should stay in API.md as documented constraints.

CLI Binary
spatialroot_realtime works correctly. Arguments are well-documented in the --help output. --list-devices is a useful addition. The CLI is the clearest working surface.

The discrepancy: README.md references realtimeMain.py (Python) as the primary CLI entry point with different flag names than the actual C++ binary's flags. The C++ binary should be documented directly as the CLI surface, not via the Python wrapper.

GUI
The GUI works (Phase 10 complete). The subprocess chain (GUI → Python → binary) means:

Python + venv must be installed
Two extra process launches per session
OSC is the only runtime control path, meaning parameter changes are fire-and-forget UDP with no acknowledgment
The GUI does not need Python for OSC — spatialroot_realtime exposes al::ParameterServer which accepts standard OSC. Any OSC library (including C++) can send to it. The Python dependency for GUI control comes solely from PySide6 (the UI framework), not from any deep integration need.

Missing: Direct C++ Host Embedding
The paper's use case of embedding EngineSession in another C++ application (like the AlloSphere multimedia app mentioned in the paper) is architecturally supported but practically difficult: no install target, AlloLib header exposure, no runtime parameter setters. A developer wanting to embed the engine must manually wire its source tree into their project.

6. What Must Change to Become a Coherent C++-First Stack
   Immediate
1. Move build orchestration out of Python.
   src/config/configCPP\*.py should become shell scripts or a top-level CMake superbuild. Currently you need the Python venv active to build the C++ tools — this is backwards. A build.sh that runs cmake on each component in order (cult_transcoder, spatial_engine/realtimeEngine) would be cleaner and has no Python dependency.

1. Fix README/docs inconsistency: document the C++ binary as the primary CLI.
   README.md documents realtimeMain.py (Python wrapper) as the entry point and references spatialroot_adm_extract (deprecated). The CLI surface is spatialroot_realtime directly. The README should document that binary's flags, not the Python wrapper's. The Python wrapper can remain as a convenience but should not be primary documentation.

1. Add CMake install targets for EngineSessionCore.
   An install() directive for EngineSessionCore and its public headers would make the C++ library claim in the paper true in practice. Without this, "C++ library API" means "copy the source files."

1. Clarify CULT's CMake: add a library target alongside the CLI.
   CULT's cult_transcoder.hpp implies an embeddable library. Add add_library(cult_transcoder_lib STATIC ...) alongside add_executable(cult-transcoder ...) so that both the CLI and a linkable library are produced.

Near-term 5. Retire runPipeline.py and its Python dependencies formally.
runPipeline.py is self-described as deprecated. Retiring it (and its imports: src/analyzeADM/, src/packageADM/) removes the only remaining reason to keep the Python LUSID library and the Python offline pipeline code. This simplifies the Python surface significantly.

6. Separate requirements.txt into runtime and dev.
   The realtime path only needs PySide6 and python-osc. Numpy, soundfile, matplotlib, pandas, jupyter, gdown should move to requirements-dev.txt or be removed when the offline pipeline is retired.

7. Replace runRealtime.py with a shell script for the ADM path.
   The ADM path in runRealtime.py does: (a) check .init_complete, (b) call cult-transcoder, (c) call spatialroot_realtime. Steps (b) and (c) are subprocess.run() calls that a shell script does more directly. The GUI can eventually launch the binary directly rather than through Python.

Later 8. Add per-parameter runtime setters to EngineSession.
The paper claims "runtime render control." Currently a C++ embedding host cannot call session.setGain(0.7) — it must send OSC to 127.0.0.1:9009. Adding setMasterGain(float), setFocus(float), setSpeakerMixDb(float) methods that write the config atomics directly would make the library API consistent with the paper's claim without changing the threading model.

9. Convert elevationMode to the ElevationMode enum in the public API.
   EngineOptions::elevationMode is int. API.md notes this as a known issue. It should be the enum.

10. Add a root-level CMake superbuild or at minimum a build.sh.
    The repo has no unified build entry point. Three separate CMake projects (cult_transcoder/, spatial_engine/realtimeEngine/, spatial_engine/spatialRender/) must be built independently. A superbuild CMake or a build.sh that runs all three in order would make the stack buildable without Python.

11. Recommended Migration Sequence
    Stage 1 — Infrastructure cleanup (no behavior changes)
    Order matters: do these before any C++ changes.

Write build.sh that runs cmake on all three C++ components in order (cult_transcoder → realtimeEngine → spatialRender). Verify it produces the same binaries as the Python build path. This decouples the build from the venv.
Update README.md to document spatialroot_realtime flags directly (not via realtimeMain.py), remove references to deprecated spatialroot_adm_extract, fix the OSC port discrepancy (9009 in code vs 12345 in README).
Split requirements.txt into runtime (PySide6, python-osc) and dev/offline (everything else).
Formally deprecate runPipeline.py with a comment block; do not delete it yet.
Why this order: these are documentation and tooling changes. No code changes, no breakage risk. They make the actual architecture visible before modifying it.

Stage 2 — CMake hardening
Add install(TARGETS EngineSessionCore) with its public headers to the realtime engine's CMakeLists.txt.
Add add_library(cult_transcoder_lib STATIC ...) alongside the CLI target in cult_transcoder/CMakeLists.txt.
Add install(TARGETS cult-transcoder) to cult_transcoder/CMakeLists.txt.
Why this order: these are additive CMake changes. No source changes. They make the library claim real without touching code.

Stage 3 — API surface hardening
Add per-parameter runtime setters to EngineSession (setMasterGain, setFocus, setSpeakerMixDb, setSubMixDb). These write the config atomics directly — same thread safety as OSC does. Document them in API.md.
Change EngineOptions::elevationMode from int to ElevationMode enum. Update CLI arg parsing in main.cpp to cast. Update API.md.
Expand API.md with the constraints from api_mismatch_ledger.md (staged setup, no seek, OSC ownership, shutdown sequence) as explicit documented constraints.
Why this order: the setters are low-risk additive changes. The enum change is a small breaking change to the API struct — do it before the API is used by external hosts. API.md updates should accompany each change.

Stage 4 — Python surface reduction
Remove src/analyzeADM/, src/packageADM/, and the Python LUSID library (LUSID/src/, LUSID/tests/) once runPipeline.py is confirmed unused.
Simplify runRealtime.py: remove the setupCppTools() call (build is now build.sh's job), remove the .init_complete check, simplify to pure subprocess launch with path validation.
Update GUI's RealtimeRunner to optionally launch spatialroot_realtime directly (bypassing the Python wrapper for the common LUSID package case).
Why this order: wait until the build is decoupled (Stage 1) before removing the Python build system. Wait until the Python LUSID library has confirmed no live callers before removing it.

8. Risks, Unknowns, and Decisions to Make Before Coding
1. Does any production deployment actually use runPipeline.py?
   It is marked deprecated in the code but appears fully documented in README.md. Before removing it and its dependencies, confirm it is not used by any AlloSphere or TransLAB deployment script.

1. What is realtimeMain.py?
   The README documents it as the primary CLI entry, but runRealtime.py is the actual implementation referenced in AGENTS.md and code. These may be the same file or one may be a wrapper. Clarify before updating documentation.

1. Should CULT be a sub-repo or a directory?
   cult_transcoder/ has its own CMakeLists.txt, its own thirdparty/, and its own internal docs. It could be extracted as a git submodule of spatialroot, which would clean the dependency graph. Decide this before adding install targets, because the install path changes if CULT becomes a submodule.

1. Can the GUI's OSC dependency be made optional?
   Currently python-osc is required for the GUI to send parameter changes. If python-osc is unavailable, the GUI can still launch the engine but cannot update parameters at runtime. Should that be a graceful degradation or a hard requirement? The code already has try: from pythonosc... except ImportError: — clarify whether this path is tested.

1. Is EngineSessionCore intended to be stable API between tool releases?
   If the engine is to be embedded by the AlloSphere multimedia app, semantic versioning and API stability guarantees are needed. This is a policy decision that should be made before publishing install targets.

1. What is the intended boundary between CULT and JSONLoader.cpp?
   JSONLoader.cpp (inside the engine) reads LUSID JSON. CULT writes LUSID JSON. These have separate implementations of the LUSID format. If CULT were a library, the engine could call CULT's parser instead of its own JSONLoader. Decide whether JSONLoader should eventually be replaced by a CULT library reader, or remain separate.

1. 64-channel limit from uint64_t bitmasks.
   EngineStatus uses uint64_t bitmasks for active channel tracking. This implicitly caps the engine at 64 output channels. The AlloSphere is 54.1 channels — within range. But API.md notes this limit is not explicitly stated. It should be documented before external adopters hit it.

1. Minimal Documentation Updates Needed
   In priority order:

1. README.md

Replace realtimeMain.py as the primary CLI with spatialroot_realtime (the C++ binary) and its actual flags.
Remove references to deprecated spatialroot_adm_extract from the setup steps.
Fix OSC port: code says 9009, README says 12345.
Clarify that runRealtime.py is a Python convenience wrapper around the C++ binary, not the primary interface. 2. PUBLIC_DOCS/API.md

Add the constraints from api_mismatch_ledger.md as a documented section (staged setup, no seek/stop, OSC ownership, shutdown sequence).
Define acceptable ranges for all numeric fields (sampleRate, bufferSize, dB trims).
State the 64-channel limit explicitly.
Describe what a C++ host must do to embed EngineSessionCore (until install targets exist: what headers to include, what CMake targets to link).
Once per-parameter setters are added (Stage 3), document them here. 3. internalDocsMD/cpp_refactor/repo_refactor.md
Currently a single empty line. This is the document the user had open — it should be the migration plan. Use the content of Section 7 above as the basis for this document.

4. cult_transcoder/internalDocsMD/DESIGN-DOC-V1-CULT.MD
   Should clarify the intended library vs CLI distinction. If a cult_transcoder_lib target is added, document what the public API surface is and how EngineSession would call it directly (even if that wiring is future work).

5. internalDocsMD/AGENTS.md
   Currently describes the stack accurately as of Phase 4/5 but still refers to the Python build system as authoritative. After build.sh is created, update the build instructions section to reflect that builds can be done without Python.
