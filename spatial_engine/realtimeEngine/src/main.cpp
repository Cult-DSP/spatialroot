// main.cpp — Real-Time Spatial Audio Engine entry point
//
// This is the CLI entry point for the real-time engine. It:
//   1. Parses command-line arguments (layout, scene, sources, etc.)
//   2. Creates the RealtimeConfig and EngineState
//   3. Loads the LUSID scene and speaker layout
//   4. Opens all source WAV files (Streaming agent)
//   5. Computes layout analysis and loads keyframes (Pose agent)
//   6. Initializes the Backend Adapter (AlloLib AudioIO)
//   7. Wires Streaming + Pose into the audio callback
//   8. Starts audio streaming
//   9. Runs a monitoring loop until interrupted (Ctrl+C, scene end, or GUI)
//  10. Shuts down cleanly (paramServer → streaming agent → backend)
//
// PHASE 10: GUI Agent — adds al::ParameterServer for live OSC control from
//   the Python GUI (gui/realtimeGUI/). Registers 7 al::Parameter objects:
//     /realtime/gain             — master gain 0.1–3.0
//     /realtime/focus            — DBAP rolloff exponent 0.2–5.0
//     /realtime/speaker_mix_db   — loudspeaker mix trim in dB (±10)
//     /realtime/sub_mix_db       — subwoofer mix trim in dB (±10)
//     /realtime/auto_comp        — focus auto-compensation toggle (bool)
//     /realtime/paused           — pause/play toggle (bool)
//     /realtime/elevation_mode   — vertical rescaling mode (0=RescaleAtmosUp,
//                                  1=RescaleFullSphere, 2=Clamp)
//   New flags: --osc_port <int> (default 9009), --focus <float> (default 1.5),
//              --elevation_mode <0|1|2> (default 0 = RescaleAtmosUp)
//   Shutdown order: paramServer.stopServer() BEFORE streaming.shutdown()
//
// PHASE 7: Output Remap Agent — adds --remap <csv> flag.
//
// Usage:
//   ./spatialroot_realtime_engine \
//       --layout ../speaker_layouts/allosphere_layout.json \
//       --scene ../../processedData/stageForRender/scene.lusid.json \
//       --sources ../../sourceData/lusid_package \
//       [--samplerate 48000] \
//       [--buffersize 512] \
//       [--gain 0.5] \
//       [--focus 1.5] \
//       [--osc_port 9009]

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include "RealtimeTypes.hpp"
#include "RealtimeBackend.hpp"
#include "Streaming.hpp"
#include "Pose.hpp"            // Pose — source position interpolation
#include "Spatializer.hpp"     // Spatializer — DBAP spatial panning (includes OutputRemap)
#include "OutputRemap.hpp"     // OutputRemap — CSV-based channel remap table
#include "JSONLoader.hpp"      // SpatialData, JSONLoader::loadLusidScene()
#include "LayoutLoader.hpp"    // SpeakerLayoutData, LayoutLoader::loadLayout()

// Phase 10 — GUI Agent: OSC parameter server
#include "al/ui/al_Parameter.hpp"
#include "al/ui/al_ParameterServer.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Signal handling for clean shutdown on Ctrl+C
// ─────────────────────────────────────────────────────────────────────────────

static RealtimeConfig* g_config = nullptr;

void signalHandler(int signum) {
    std::cout << "\n[Main] Interrupt received (signal " << signum << "). Shutting down..." << std::endl;
    if (g_config) {
        g_config->shouldExit.store(true);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Argument parsing helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Look up a string argument by name. Returns empty string if not found.
static std::string getArgString(int argc, char* argv[], const std::string& flag) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == flag) {
            return std::string(argv[i + 1]);
        }
    }
    return "";
}

/// Look up an integer argument by name. Returns defaultVal if not found.
static int getArgInt(int argc, char* argv[], const std::string& flag, int defaultVal) {
    std::string val = getArgString(argc, argv, flag);
    if (!val.empty()) {
        try { return std::stoi(val); }
        catch (...) { return defaultVal; }
    }
    return defaultVal;
}

/// Look up a float argument by name. Returns defaultVal if not found.
static float getArgFloat(int argc, char* argv[], const std::string& flag, float defaultVal) {
    std::string val = getArgString(argc, argv, flag);
    if (!val.empty()) {
        try { return std::stof(val); }
        catch (...) { return defaultVal; }
    }
    return defaultVal;
}

/// Check if a flag is present (no value).
static bool hasArg(int argc, char* argv[], const std::string& flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == flag) return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Usage / help
// ─────────────────────────────────────────────────────────────────────────────

static void printUsage(const char* progName) {
    std::cout << "\nspatialroot Real-Time Spatial Audio Engine (Phase 10 — GUI Agent)\n"
              << "─────────────────────────────────────────────────────────────────\n"
              << "Usage: " << progName << " [options]\n\n"
              << "Required:\n"
              << "  --layout <path>     Speaker layout JSON file\n"
              << "  --scene <path>      LUSID scene JSON file (positions/trajectories)\n\n"
              << "Source input (one of the following is required):\n"
              << "  --sources <path>    Folder containing mono source WAV files\n"
              << "  --adm <path>        Multichannel ADM WAV file (direct streaming,\n"
              << "                      skips stem splitting)\n\n"
              << "Optional:\n"
              << "  --samplerate <int>  Audio sample rate in Hz (default: 48000)\n"
              << "  --buffersize <int>  Frames per audio callback (default: 512)\n"
              << "  --gain <float>      Master gain 0.0–1.0 (default: 0.5)\n"
              << "  --focus <float>     DBAP rolloff exponent 0.2–5.0 (default: 1.5)\n"
              << "  --speaker_mix <dB>  Loudspeaker mix trim in dB (±10, default: 0)\n"
              << "  --sub_mix <dB>      Subwoofer mix trim in dB (±10, default: 0)\n"
              << "  --auto_compensation Enable focus auto-compensation (default: off)\n"
              << "  --elevation_mode <n> Vertical rescaling mode (default: 0):\n"
              << "                       0 = RescaleAtmosUp   (Atmos [0,+90°] → layout)\n"
              << "                       1 = RescaleFullSphere ([-90°,+90°] → layout)\n"
              << "                       2 = Clamp            (hard clip to layout bounds)\n"
              << "  --remap <path>      CSV file mapping internal layout channels to device\n"
              << "                      output channels (default: identity, no remapping)\n"
              << "                      CSV format: 'layout,device' (0-based, headers required)\n"
              << "  --osc_port <int>    UDP port for al::ParameterServer OSC control\n"
              << "                      (default: 9009). GUI sends to this port.\n"
              << "  --help              Show this message\n"
              << "\nNote: Output channel count is derived automatically from the speaker\n"
              << "layout (speakers + subwoofers). No manual channel count needed.\n"
              << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {

    // ── Help flag ────────────────────────────────────────────────────────
    if (hasArg(argc, argv, "--help") || hasArg(argc, argv, "-h")) {
        printUsage(argv[0]);
        return 0;
    }

    std::cout << "\n╔══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  spatialroot Real-Time Spatial Audio Engine  (Phase 10)   ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════╝\n" << std::endl;

    // ── Parse arguments ──────────────────────────────────────────────────

    RealtimeConfig config;
    EngineState    state;

    config.layoutPath    = getArgString(argc, argv, "--layout");
    config.scenePath     = getArgString(argc, argv, "--scene");
    config.sourcesFolder = getArgString(argc, argv, "--sources");
    config.admFile       = getArgString(argc, argv, "--adm");
    config.sampleRate    = getArgInt(argc, argv, "--samplerate", 48000);
    config.bufferSize    = getArgInt(argc, argv, "--buffersize", 512);
    config.masterGain.store(getArgFloat(argc, argv, "--gain", 0.5f));
    // Phase 6: Compensation and Gain Agent
    float speakerMixDb = getArgFloat(argc, argv, "--speaker_mix", 0.0f);
    config.loudspeakerMix.store(powf(10.0f, speakerMixDb / 20.0f));
    float subMixDb = getArgFloat(argc, argv, "--sub_mix", 0.0f);
    config.subMix.store(powf(10.0f, subMixDb / 20.0f));
    config.focusAutoCompensation.store(hasArg(argc, argv, "--auto_compensation"));
    // NOTE: outputChannels is computed from the speaker layout (see Spatializer::init).
    //       No --channels flag needed.

    // Phase 10: focus exponent + OSC port
    config.dbapFocus.store(getArgFloat(argc, argv, "--focus", 1.5f), std::memory_order_relaxed);
    int oscPort = getArgInt(argc, argv, "--osc_port", 9009);

    // Elevation mode: 0=RescaleAtmosUp (default), 1=RescaleFullSphere, 2=Clamp
    {
        int elModeInt = getArgInt(argc, argv, "--elevation_mode", 0);
        elModeInt = std::max(0, std::min(2, elModeInt));  // clamp to valid range
        config.elevationMode.store(elModeInt, std::memory_order_relaxed);
    }

    // pendingAutoComp: set by ParameterServer callback (listener thread) when
    // /realtime/auto_comp changes; consumed on the main thread in the monitoring
    // loop so that computeFocusCompensation() is always called from MAIN.
    std::atomic<bool> pendingAutoComp{false};

    // Determine input mode
    bool useADM = !config.admFile.empty();
    bool useMono = !config.sourcesFolder.empty();

    // ── Validate required arguments ──────────────────────────────────────

    if (config.layoutPath.empty()) {
        std::cerr << "[Main] ERROR: --layout is required." << std::endl;
        printUsage(argv[0]);
        return 1;
    }
    if (config.scenePath.empty()) {
        std::cerr << "[Main] ERROR: --scene is required." << std::endl;
        printUsage(argv[0]);
        return 1;
    }
    if (!useADM && !useMono) {
        std::cerr << "[Main] ERROR: Either --sources or --adm is required." << std::endl;
        printUsage(argv[0]);
        return 1;
    }
    if (useADM && useMono) {
        std::cerr << "[Main] ERROR: --sources and --adm are mutually exclusive." << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    std::cout << "[Main] Configuration:" << std::endl;
    std::cout << "  Layout:       " << config.layoutPath << std::endl;
    std::cout << "  Scene:        " << config.scenePath << std::endl;
    if (useADM) {
        std::cout << "  ADM file:     " << config.admFile << " (direct streaming)" << std::endl;
    } else {
        std::cout << "  Sources:      " << config.sourcesFolder << " (mono files)" << std::endl;
    }
    std::cout << "  Sample rate:  " << config.sampleRate << " Hz" << std::endl;
    std::cout << "  Buffer size:  " << config.bufferSize << " frames" << std::endl;
    std::cout << "  Master gain:  " << config.masterGain.load() << std::endl;
    std::cout << "  Speaker mix:  " << config.loudspeakerMix.load() << " (" << speakerMixDb << " dB)" << std::endl;
    std::cout << "  Sub mix:      " << config.subMix.load() << " (" << subMixDb << " dB)" << std::endl;
    std::cout << "  Auto-comp:    " << (config.focusAutoCompensation.load() ? "enabled" : "disabled") << std::endl;
    std::cout << "  Focus:        " << config.dbapFocus.load() << std::endl;
    std::cout << "  OSC port:     " << oscPort << std::endl;
    {
        static const char* elModeNames[] = {"RescaleAtmosUp", "RescaleFullSphere", "Clamp"};
        int em = config.elevationMode.load(std::memory_order_relaxed);
        std::cout << "  Elevation:    " << elModeNames[em] << " (mode " << em << ")" << std::endl;
    }
    std::cout << "  (Output channels will be derived from speaker layout)" << std::endl;
    std::cout << std::endl;

    // ── Register signal handler for clean Ctrl+C shutdown ────────────────
    g_config = &config;
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // ── Phase 2: Load LUSID scene and open source WAV files ──────────────

    std::cout << "[Main] Loading LUSID scene: " << config.scenePath << std::endl;
    SpatialData scene;
    try {
        scene = JSONLoader::loadLusidScene(config.scenePath);
    } catch (const std::exception& e) {
        std::cerr << "[Main] FATAL: Failed to load LUSID scene: " << e.what() << std::endl;
        return 1;
    }
    std::cout << "[Main] Scene loaded: " << scene.sources.size()
              << " sources";
    if (scene.duration > 0) {
        std::cout << ", duration: " << scene.duration << "s";
    }
    std::cout << "." << std::endl;

    // Create the streaming agent and load source WAVs
    Streaming streaming(config, state);
    if (useADM) {
        // ADM direct streaming mode — read from multichannel file
        if (!streaming.loadSceneFromADM(scene, config.admFile)) {
            std::cerr << "[Main] FATAL: No source channels could be loaded from ADM." << std::endl;
            return 1;
        }
    } else {
        // Mono file mode — read from individual WAV files
        if (!streaming.loadScene(scene)) {
            std::cerr << "[Main] FATAL: No source files could be loaded." << std::endl;
            return 1;
        }
    }

    // Store scene duration (longest source determines total length)
    // The streaming agent already populated numSources in engine state.
    std::cout << "[Main] " << streaming.numSources() << " sources ready for streaming."
              << std::endl;

    // ── Phase 3: Load speaker layout and create Pose agent ───────────────

    std::cout << "[Main] Loading speaker layout: " << config.layoutPath << std::endl;
    SpeakerLayoutData layout;
    try {
        layout = LayoutLoader::loadLayout(config.layoutPath);
    } catch (const std::exception& e) {
        std::cerr << "[Main] FATAL: Failed to load speaker layout: " << e.what() << std::endl;
        return 1;
    }
    std::cout << "[Main] Layout loaded: " << layout.speakers.size()
              << " speakers, " << layout.subwoofers.size() << " subwoofers."
              << std::endl;

    // Create the pose agent and load keyframes + layout analysis
    Pose pose(config, state);
    if (!pose.loadScene(scene, layout)) {
        std::cerr << "[Main] FATAL: Pose agent failed to initialize." << std::endl;
        return 1;
    }
    std::cout << "[Main] Pose agent ready: " << pose.numSources()
              << " source positions will be computed per block." << std::endl;

    // ── Phase 4: Create Spatializer (DBAP) ───────────────────────────────

    Spatializer spatializer(config, state);
    if (!spatializer.init(layout)) {
        std::cerr << "[Main] FATAL: Spatializer initialization failed." << std::endl;
        return 1;
    }
    std::cout << "[Main] Spatializer ready: DBAP with " << spatializer.numSpeakers()
              << " speakers, focus=" << config.dbapFocus.load() << "." << std::endl;
    std::cout << "[Main] Output channels (from layout): " << config.outputChannels << std::endl;

    // Fix 1 — Preallocate per-source onset-fade state.
    // Must run after pose.loadScene() (which sets the source count) and
    // before backend.start(). Uses the same stable slot ordering as mPoses.
    spatializer.prepareForSources(pose.numSources());

    // ── Phase 6: Compensation and Gain ───────────────────────────────────
    // If focus auto-compensation is enabled, compute the initial loudspeaker
    // mix trim for the current focus value now (on the main thread, before
    // audio starts). This sets mConfig.loudspeakerMix to the right value
    // so the very first audio block already has the compensation applied.
    if (config.focusAutoCompensation.load()) {
        std::cout << "[Main] Focus auto-compensation ON — computing initial autoCompValue..." << std::endl;
        float comp = spatializer.computeFocusCompensation();
        std::cout << "[Main] Initial auto-compensation: " << comp
                  << " (" << (20.0f * std::log10(comp)) << " dB)"
                  << " — stored in mAutoCompValue, mConfig.loudspeakerMix unchanged." << std::endl;
    }
    std::cout << "[Main] Phase 6 gains: loudspeakerMix=" << config.loudspeakerMix.load()
              << " (" << (20.0f * std::log10(config.loudspeakerMix.load())) << " dB)"
              << "  subMix=" << config.subMix.load()
              << " (" << (20.0f * std::log10(config.subMix.load())) << " dB)" << std::endl;

    // ── Phase 7: Output Remap ─────────────────────────────────────────────
    // Load the CSV remap table if --remap was provided. The OutputRemap
    // object is created here (on the main thread), loaded once, then handed
    // to the Spatializer as a const pointer. The audio callback reads it
    // without any locking (immutable during playback).
    //
    // If --remap is omitted the Spatializer uses its identity fast-path
    // (same behaviour as Phase 6 and earlier — zero overhead change).
    std::string remapPath = getArgString(argc, argv, "--remap");
    OutputRemap outputRemap;
    if (!remapPath.empty()) {
        std::cout << "[Main] Loading output remap CSV: " << remapPath << std::endl;
        bool remapOk = outputRemap.load(remapPath,
                                        config.outputChannels,
                                        config.outputChannels);
        if (!remapOk) {
            std::cout << "[Main] Remap load failed or resulted in identity — continuing with identity mapping." << std::endl;
        }
        spatializer.setRemap(&outputRemap);
    } else {
        std::cout << "[Main] No --remap provided — using identity channel mapping." << std::endl;
    }

    // ── Phase 10: ParameterServer (OSC / live GUI control) ───────────────
    // Declare 6 al::Parameter objects seeded from CLI values.
    // The ParameterServer listens on 127.0.0.1:oscPort and dispatches
    // incoming OSC messages to the registered callbacks. Callbacks run on
    // the ParameterServer listener thread — they MUST only write atomics
    // or set the pendingAutoComp flag; no heap allocation, no I/O.

    al::Parameter     gainParam     {"gain",           "realtime",
                                     config.masterGain.load(),  0.1f,  3.0f};
    al::Parameter     focusParam    {"focus",          "realtime",
                                     config.dbapFocus.load(),   0.2f,  5.0f};
    al::Parameter     spkMixDbParam {"speaker_mix_db", "realtime",
                                     speakerMixDb,             -10.0f, 10.0f};
    al::Parameter     subMixDbParam {"sub_mix_db",     "realtime",
                                     subMixDb,                 -10.0f, 10.0f};
    al::ParameterBool autoCompParam {"auto_comp",      "realtime",
                                     config.focusAutoCompensation.load() ? 1.0f : 0.0f};
    al::ParameterBool pausedParam   {"paused",         "realtime", 0.0f};
    // elevation_mode: float carrying an integer value 0/1/2 (al::Parameter
    // doesn't have an int specialization — float is idiomatic in AlloLib OSC).
    al::Parameter     elevModeParam {"elevation_mode", "realtime",
                                     static_cast<float>(config.elevationMode.load(
                                         std::memory_order_relaxed)),
                                     0.0f, 2.0f};

    // Register change callbacks ─────────────────────────────────────────
    // THREADING: All callbacks fire on the ParameterServer listener thread.
    // Only relaxed atomic stores and flag sets are performed here.

    gainParam.registerChangeCallback([&](float v) {
        config.masterGain.store(v, std::memory_order_relaxed);
        std::cout << "\n[OSC] gain → " << v << std::flush;
    });

    focusParam.registerChangeCallback([&](float v) {
        config.dbapFocus.store(v, std::memory_order_relaxed);  // atomic store — no data race
        // Queue recomputation; main thread will call computeFocusCompensation().
        if (config.focusAutoCompensation.load(std::memory_order_relaxed)) {
            pendingAutoComp.store(true, std::memory_order_relaxed);
        }
        std::cout << "\n[OSC] focus → " << v << std::flush;
    });

    spkMixDbParam.registerChangeCallback([&](float dB) {
        config.loudspeakerMix.store(powf(10.0f, dB / 20.0f),
                                    std::memory_order_relaxed);
        std::cout << "\n[OSC] speaker_mix_db → " << dB << " dB" << std::flush;
    });

    subMixDbParam.registerChangeCallback([&](float dB) {
        config.subMix.store(powf(10.0f, dB / 20.0f),
                            std::memory_order_relaxed);
        std::cout << "\n[OSC] sub_mix_db → " << dB << " dB" << std::flush;
    });

    autoCompParam.registerChangeCallback([&](float v) {
        bool enable = (v >= 0.5f);
        config.focusAutoCompensation.store(enable, std::memory_order_relaxed);
        if (enable) pendingAutoComp.store(true, std::memory_order_relaxed);
        std::cout << "\n[OSC] auto_comp → " << (enable ? "on" : "off") << std::flush;
    });

    pausedParam.registerChangeCallback([&](float v) {
        bool p = (v >= 0.5f);
        config.paused.store(p, std::memory_order_relaxed);
        std::cout << "\n[OSC] paused → " << (p ? "paused" : "playing") << std::flush;
    });

    elevModeParam.registerChangeCallback([&](float v) {
        int mode = static_cast<int>(std::round(v));
        mode = std::max(0, std::min(2, mode));   // guard against out-of-range
        config.elevationMode.store(mode, std::memory_order_relaxed);
        static const char* names[] = {"RescaleAtmosUp", "RescaleFullSphere", "Clamp"};
        std::cout << "\n[OSC] elevation_mode → " << names[mode]
                  << " (" << mode << ")" << std::flush;
    });

    // Start the ParameterServer ─────────────────────────────────────────
    al::ParameterServer paramServer{"127.0.0.1", oscPort};
    paramServer << gainParam << focusParam << spkMixDbParam
                << subMixDbParam << autoCompParam << pausedParam
                << elevModeParam;

    if (!paramServer.serverRunning()) {
        std::cerr << "[Main] FATAL: ParameterServer failed to start on port "
                  << oscPort << ". Is the port already in use?" << std::endl;
        return 1;
    }
    std::cout << "[Main] ParameterServer listening on 127.0.0.1:" << oscPort << std::endl;
    paramServer.print();

    // ── Initialize the Backend Adapter ───────────────────────────────────

    RealtimeBackend backend(config, state);

    if (!backend.init()) {
        std::cerr << "[Main] FATAL: Backend initialization failed." << std::endl;
        return 1;
    }

    // Wire all agents into the audio callback
    backend.setStreaming(&streaming);
    backend.setPose(&pose);
    backend.setSpatializer(&spatializer);
    backend.cacheSourceNames(streaming.sourceNames());

    // Start the background loader thread BEFORE audio begins.
    // This ensures the first buffer swap is ready when the callback fires.
    streaming.startLoader();

    if (!backend.start()) {
        std::cerr << "[Main] FATAL: Backend failed to start." << std::endl;
        streaming.shutdown();
        return 1;
    }

    // ── Monitoring loop ──────────────────────────────────────────────────
    // Run until Ctrl+C or shouldExit is set. Print status every second.
    // This is where the GUI event loop would go in the future.

    std::cout << "[Main] DBAP spatialization active: " << streaming.numSources()
              << " sources → " << spatializer.numSpeakers()
              << " speakers. Press Ctrl+C to stop.\n" << std::endl;

    // ── One-time device/channel info (printed before loop, static after init) ──
    {
        const int reqCh    = config.outputChannels;
        const int actCh    = static_cast<int>(backend.audioIO().channelsOutDevice());
        const int renderCh = static_cast<int>(spatializer.numRenderChannels());
        std::cout << "[Diag] Channels — requested: " << reqCh
                  << "  actual-device: " << actCh
                  << "  render-bus: " << renderCh << std::endl;
    }

    while (!config.shouldExit.load()) {

        // ── Consume pendingAutoComp on the MAIN thread ────────────────────
        // computeFocusCompensation() allocates internally, so it MUST NOT be
        // called from the ParameterServer callback (listener thread). The
        // callback sets pendingAutoComp; we pick it up here.
        if (pendingAutoComp.load(std::memory_order_relaxed)) {
            pendingAutoComp.store(false, std::memory_order_relaxed);
            float comp = spatializer.computeFocusCompensation();
            std::cout << "\n[Main] Focus compensation recomputed: autoCompValue="
                      << comp
                      << " (" << (20.0f * std::log10(comp)) << " dB)" << std::flush;
        }

        // ── Phase 14: one-shot relocation event detection ─────────────────
        // Four latches, printed in pairs so pre/post-copy can be compared:
        //
        //   [RELOC-RENDER] / [RELOC-DEVICE] — absolute-mask changes (any channel
        //     crossing −80 dBFS). Broad; first-block false positive suppressed.
        //
        //   [DOM-RENDER] / [DOM-DEVICE] — dominant-mask changes (top-energy
        //     speaker cluster, −20 dBFS relative threshold). Fewer false
        //     positives; changes here are more likely to be audible relocations.
        //
        // If DOM fires without RELOC, only the dominant cluster shifted (likely
        // audible). If only RELOC fires, it is probably a far-field bleed edge.
        double timeSec = state.playbackTimeSec.load(std::memory_order_relaxed);

        if (state.renderRelocEvent.load(std::memory_order_relaxed)) {
            uint64_t prev = state.renderRelocPrev.load(std::memory_order_relaxed);
            uint64_t curr = state.renderRelocNext.load(std::memory_order_relaxed);
            state.renderRelocEvent.store(false, std::memory_order_relaxed);
            std::cout << "\n[RELOC-RENDER] t=" << std::fixed;
            std::cout.precision(2);
            std::cout << timeSec << "s  mask: 0x" << std::hex << prev
                      << " → 0x" << curr << std::dec << std::endl;
        }
        if (state.deviceRelocEvent.load(std::memory_order_relaxed)) {
            uint64_t prev = state.deviceRelocPrev.load(std::memory_order_relaxed);
            uint64_t curr = state.deviceRelocNext.load(std::memory_order_relaxed);
            state.deviceRelocEvent.store(false, std::memory_order_relaxed);
            std::cout << "\n[RELOC-DEVICE] t=" << std::fixed;
            std::cout.precision(2);
            std::cout << timeSec << "s  mask: 0x" << std::hex << prev
                      << " → 0x" << curr << std::dec << std::endl;
        }
        if (state.renderDomRelocEvent.load(std::memory_order_relaxed)) {
            uint64_t prev = state.renderDomRelocPrev.load(std::memory_order_relaxed);
            uint64_t curr = state.renderDomRelocNext.load(std::memory_order_relaxed);
            state.renderDomRelocEvent.store(false, std::memory_order_relaxed);
            std::cout << "\n[DOM-RENDER]   t=" << std::fixed;
            std::cout.precision(2);
            std::cout << timeSec << "s  dom: 0x" << std::hex << prev
                      << " → 0x" << curr << std::dec << std::endl;
        }
        if (state.deviceDomRelocEvent.load(std::memory_order_relaxed)) {
            uint64_t prev = state.deviceDomRelocPrev.load(std::memory_order_relaxed);
            uint64_t curr = state.deviceDomRelocNext.load(std::memory_order_relaxed);
            state.deviceDomRelocEvent.store(false, std::memory_order_relaxed);
            std::cout << "\n[DOM-DEVICE]   t=" << std::fixed;
            std::cout.precision(2);
            std::cout << timeSec << "s  dom: 0x" << std::hex << prev
                      << " → 0x" << curr << std::dec << std::endl;
        }

        // ── Status line every ~500 ms ─────────────────────────────────────
        float  cbCpu    = state.callbackCpuLoad.load(std::memory_order_relaxed);
        bool   paused   = config.paused.load(std::memory_order_relaxed);
        uint64_t rMask  = state.renderActiveMask.load(std::memory_order_relaxed);
        uint64_t dMask  = state.deviceActiveMask.load(std::memory_order_relaxed);
        uint64_t rDom   = state.renderDomMask.load(std::memory_order_relaxed);
        uint64_t dDom   = state.deviceDomMask.load(std::memory_order_relaxed);
        float  mainRms  = state.mainRmsTotal.load(std::memory_order_relaxed);
        float  subRms   = state.subRmsTotal.load(std::memory_order_relaxed);

        std::cout << "\r  t=";
        std::cout << std::fixed;
        std::cout.precision(1);
        std::cout << timeSec << "s"
                  << "  CPU=" << std::fixed;
        std::cout.precision(1);
        std::cout << (cbCpu * 100.0f) << "%"
                  << "  rDom=0x" << std::hex << rDom
                  << "  dDom=0x" << dDom
                  << "  rBus=0x" << rMask
                  << "  dev=0x"  << dMask << std::dec
                  << "  mainRms=" << std::fixed;
        std::cout.precision(4);
        std::cout << mainRms
                  << "  subRms=" << subRms
                  << "  Xrun=" << streaming.totalUnderruns()
                  << "  NaN=" << state.nanGuardCount.load(std::memory_order_relaxed)
                  << "  SpkG=" << state.speakerProximityCount.load(std::memory_order_relaxed)
                  << "  " << (paused ? "PAUSED " : "PLAYING")
                  << "     " << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << std::endl;

    // ── Clean shutdown ───────────────────────────────────────────────────
    // Order matters:
    //   1. paramServer.stopServer() — stop OSC listener thread first so no
    //      more callbacks fire after we start tearing down state.
    //   2. backend.shutdown()       — stop audio callback; no more processBlock().
    //   3. streaming.shutdown()     — free source buffers last.

    std::cout << "\n[Main] Shutting down..." << std::endl;
    paramServer.stopServer();
    backend.shutdown();
    streaming.shutdown();

    std::cout << "[Main] Final stats:" << std::endl;
    std::cout << "  Total frames: " << state.frameCounter.load() << std::endl;
    std::cout << "  Total time:   " << state.playbackTimeSec.load() << " seconds" << std::endl;
    std::cout << "[Main] Goodbye." << std::endl;

    return 0;
}

