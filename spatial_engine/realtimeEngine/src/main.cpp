#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cmath>
#include <iomanip>

#include "RealtimeTypes.hpp"
#include "EngineSession.hpp"

// Phase 10 — GUI Agent: OSC parameter server
// Only needed for list-devices in main, actually AlloLib AudioDevice is used below.
#include "al/io/al_AudioIO.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Signal handling for clean shutdown on Ctrl+C
// ─────────────────────────────────────────────────────────────────────────────

static std::atomic<bool> g_shouldExit{false};

void signalHandler(int signum) {
    std::cout << "\n[Main] Interrupt received (signal " << signum << "). Shutting down..." << std::endl;
    g_shouldExit.store(true, std::memory_order_relaxed);
}

// ─────────────────────────────────────────────────────────────────────────────
// Argument parsing helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string getArgString(int argc, char* argv[], const std::string& flag) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == flag) {
            return std::string(argv[i + 1]);
        }
    }
    return "";
}

static int getArgInt(int argc, char* argv[], const std::string& flag, int defaultVal) {
    std::string val = getArgString(argc, argv, flag);
    if (!val.empty()) {
        try { return std::stoi(val); }
        catch (...) { return defaultVal; }
    }
    return defaultVal;
}

static float getArgFloat(int argc, char* argv[], const std::string& flag, float defaultVal) {
    std::string val = getArgString(argc, argv, flag);
    if (!val.empty()) {
        try { return std::stof(val); }
        catch (...) { return defaultVal; }
    }
    return defaultVal;
}

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
    std::cout << "\nspatialroot Real-Time Spatial Audio Engine (Session API)\n"
              << "─────────────────────────────────────────────────────────────────\n"
              << "Usage: " << progName << " [options]\n\n"
              << "Required:\n"
              << "  --layout <path>     Speaker layout JSON file\n"
              << "  --scene <path>      LUSID scene JSON file (positions/trajectories)\n\n"
              << "Source input (one of the following is required):\n"
              << "  --sources <path>    Folder containing mono source WAV files\n"
              << "  --adm <path>        Multichannel ADM WAV file\n\n"
              << "Optional:\n"
              << "  --samplerate <int>  Audio sample rate in Hz (default: 48000)\n"
              << "  --buffersize <int>  Frames per audio callback (default: 512)\n"
              << "  --gain <float>      Master gain 0.0–1.0 (default: 0.5)\n"
              << "  --focus <float>     DBAP rolloff exponent 0.2–5.0 (default: 1.5)\n"
              << "  --speaker_mix <dB>  Loudspeaker mix trim in dB (±10, default: 0)\n"
              << "  --sub_mix <dB>      Subwoofer mix trim in dB (±10, default: 0)\n"
              << "  --auto_compensation Enable focus auto-compensation (default: off)\n"
              << "  --elevation_mode <n> Vertical rescaling mode (default: 0):\n"
              << "                       0 = RescaleAtmosUp, 1 = RescaleFullSphere, 2 = Clamp\n"
              << "  --remap <path>      CSV file mapping internal layout channels to device\n"
              << "  --osc_port <int>    UDP port for al::ParameterServer OSC control (default: 9009)\n"
              << "  --device <name>     Exact name of the output audio device to open.\n"
              << "  --list-devices      List available output audio devices and exit.\n"
              << "  --help              Show this message\n\n"
              << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {

    if (hasArg(argc, argv, "--help") || hasArg(argc, argv, "-h")) {
        printUsage(argv[0]);
        return 0;
    }

    if (hasArg(argc, argv, "--list-devices")) {
        std::cout << "\nAvailable output audio devices:" << std::endl;
        const int nDev = al::AudioDevice::numDevices();
        int outCount = 0;
        for (int i = 0; i < nDev; ++i) {
            al::AudioDevice dev(i);
            if (dev.valid() && dev.channelsOutMax() > 0) {
                std::cout << "  [" << i << "] \"" << dev.name() << "\""
                          << "  (" << dev.channelsOutMax() << " out ch)" << std::endl;
                ++outCount;
            }
        }
        if (outCount == 0) {
            std::cout << "  (no output devices found)" << std::endl;
        }
        return 0;
    }

    std::cout << "\n╔══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  spatialroot Real-Time Spatial Audio Engine (Session API)║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════╝\n" << std::endl;

    EngineSession session;
    RealtimeConfig& config = session.config();

    config.layoutPath    = getArgString(argc, argv, "--layout");
    config.scenePath     = getArgString(argc, argv, "--scene");
    config.sourcesFolder = getArgString(argc, argv, "--sources");
    config.admFile       = getArgString(argc, argv, "--adm");
    config.sampleRate    = getArgInt(argc, argv, "--samplerate", 48000);
    config.bufferSize    = getArgInt(argc, argv, "--buffersize", 512); 
    
    config.masterGain.store(getArgFloat(argc, argv, "--gain", 0.5f));
    float speakerMixDb = getArgFloat(argc, argv, "--speaker_mix", 0.0f);
    config.loudspeakerMix.store(powf(10.0f, speakerMixDb / 20.0f));
    float subMixDb = getArgFloat(argc, argv, "--sub_mix", 0.0f);
    config.subMix.store(powf(10.0f, subMixDb / 20.0f));
    config.focusAutoCompensation.store(hasArg(argc, argv, "--auto_compensation"));
    
    config.dbapFocus.store(getArgFloat(argc, argv, "--focus", 1.5f), std::memory_order_relaxed);
    int oscPort = getArgInt(argc, argv, "--osc_port", 9009);

    config.outputDeviceName = getArgString(argc, argv, "--device");
    std::string remapCsv = getArgString(argc, argv, "--remap");

    int elModeInt = getArgInt(argc, argv, "--elevation_mode", 0);
    elModeInt = std::max(0, std::min(2, elModeInt));
    config.elevationMode.store(elModeInt, std::memory_order_relaxed);

    bool useADM = !config.admFile.empty();
    bool useMono = !config.sourcesFolder.empty();

    if (config.layoutPath.empty() || config.scenePath.empty() || (!useADM && !useMono) || (useADM && useMono)) {
        std::cerr << "[Main] ERROR: Invalid arguments." << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    if (!session.configureEngine()) return 1;
    if (!session.loadScene()) return 1;
    if (!session.applyLayout()) return 1;
    if (!session.configureRuntime(oscPort, remapCsv)) return 1;
    if (!session.start()) return 1;

    std::cout << "[Main] Engine started successfully. Press Ctrl+C to stop.\n" << std::endl;

    while (!g_shouldExit.load(std::memory_order_relaxed) && !session.config().shouldExit.load(std::memory_order_relaxed)) {
        
        session.update(); // Consumes pending focus compensation properly on main thread

        EngineStatus status = session.queryStatus();
        DiagnosticEvents ev = session.consumeDiagnostics();

        double timeSec = status.timeSec;
        constexpr float kEventRmsGate = 0.005f;
        float mainRmsGate = status.mainRms;

        if (ev.renderRelocEvent) {
            std::cout << "\n[RELOC-RENDER] t=" << std::fixed << std::setprecision(2)
                      << timeSec << "s  mask: 0x" << std::hex << ev.renderRelocPrev
                      << " → 0x" << ev.renderRelocNext << std::dec << std::endl;
        }
        if (ev.deviceRelocEvent) {
            std::cout << "\n[RELOC-DEVICE] t=" << std::fixed << std::setprecision(2)
                      << timeSec << "s  mask: 0x" << std::hex << ev.deviceRelocPrev
                      << " → 0x" << ev.deviceRelocNext << std::dec << std::endl;
        }
        if (ev.renderDomRelocEvent && mainRmsGate > kEventRmsGate) {
            std::cout << "\n[DOM-RENDER]   t=" << std::fixed << std::setprecision(2)
                      << timeSec << "s  dom: 0x" << std::hex << ev.renderDomRelocPrev
                      << " → 0x" << ev.renderDomRelocNext << std::dec << std::endl;
        }
        if (ev.deviceDomRelocEvent && mainRmsGate > kEventRmsGate) {
            std::cout << "\n[DOM-DEVICE]   t=" << std::fixed << std::setprecision(2)
                      << timeSec << "s  dom: 0x" << std::hex << ev.deviceDomRelocPrev
                      << " → 0x" << ev.deviceDomRelocNext << std::dec << std::endl;
        }
        if (ev.renderClusterEvent && mainRmsGate > kEventRmsGate) {
            std::cout << "\n[CLUSTER-RENDER] t=" << std::fixed << std::setprecision(2)
                      << timeSec << "s  top4: 0x" << std::hex << ev.renderClusterPrev
                      << " → 0x" << ev.renderClusterNext << std::dec << std::endl;
        }
        if (ev.deviceClusterEvent && mainRmsGate > kEventRmsGate) {
            std::cout << "\n[CLUSTER-DEVICE] t=" << std::fixed << std::setprecision(2)
                      << timeSec << "s  top4: 0x" << std::hex << ev.deviceClusterPrev
                      << " → 0x" << ev.deviceClusterNext << std::dec << std::endl;
        }

        std::cout << "\r  t=" << std::fixed << std::setprecision(1) << timeSec << "s"
                  << "  CPU=" << (status.cpuLoad * 100.0f) << "%"
                  << "  rDom=0x" << std::hex << status.renderDomMask
                  << "  dDom=0x" << status.deviceDomMask
                  << "  rBus=0x" << status.renderActiveMask
                  << "  dev=0x" << status.deviceActiveMask << std::dec
                  << "  mainRms=" << std::fixed << std::setprecision(4) << status.mainRms
                  << "  subRms=" << status.subRms
                  << "  Xrun=" << status.xruns
                  << "  NaN=" << status.nanGuardCount
                  << "  SpkG=" << status.speakerProximityCount
                  << "  " << (status.paused ? "PAUSED " : "PLAYING")
                  << "     " << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << std::endl;
    std::cout << "\n[Main] Shutting down session..." << std::endl;
    session.shutdown();

    std::cout << "[Main] Goodbye." << std::endl;
    return 0;
}
