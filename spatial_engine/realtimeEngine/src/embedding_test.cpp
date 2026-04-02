// embedding_test.cpp — Stage 2 embedding verification (Option A)
//
// Verifies that EngineSessionCore can be linked and its full public API surface
// called from a host without any OSC dependency (oscPort = 0).
//
// This test does NOT require audio hardware or test data files. It exercises
// the lifecycle methods and all V1.1 runtime setter methods. configureEngine()
// succeeds; loadScene() will fail (no scene file provided) and the test handles
// that gracefully. The setters are called directly to verify they compile and link.
//
// Stage 2 completion bar: this file compiles, links EngineSessionCore, calls the
// full lifecycle, calls all six runtime setter methods, reads queryStatus() and
// consumeDiagnostics(), and calls shutdown().

#include "EngineSession.hpp"
#include "RealtimeTypes.hpp"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "[embedding_test] Stage 2 embedding API verification\n";
    std::cout << "[embedding_test] oscPort = 0 (no OSC dependency)\n\n";

    EngineSession session;

    // ── configureEngine ───────────────────────────────────────────────────
    EngineOptions opts;
    opts.sampleRate    = 48000;
    opts.bufferSize    = 512;
    opts.oscPort       = 0;  // disable OSC — the core requirement for embedding
    opts.elevationMode = ElevationMode::RescaleAtmosUp;

    bool ok = session.configureEngine(opts);
    std::cout << "[embedding_test] configureEngine: " << (ok ? "OK" : "FAIL") << "\n";
    assert(ok && "configureEngine must succeed with valid options");

    // ── loadScene (expected to fail — no scene file) ──────────────────────
    SceneInput scene;
    scene.scenePath     = "__nonexistent_scene__.lusid.json";
    scene.sourcesFolder = "__nonexistent_sources__";

    ok = session.loadScene(scene);
    std::cout << "[embedding_test] loadScene (expected FAIL): "
              << (!ok ? "FAIL (correct)" : "unexpected OK") << "\n";
    std::cout << "[embedding_test]   error: " << session.getLastError() << "\n\n";

    // ── V1.1 runtime setter surface ───────────────────────────────────────
    // Calling setters before start() writes the atomics (harmless, no effect on engine).
    // Primary purpose here: verify all six methods compile and link against EngineSessionCore.
    std::cout << "[embedding_test] Calling V1.1 runtime setters (pre-start, writes atomics):\n";

    session.setMasterGain(0.8f);
    std::cout << "[embedding_test]   setMasterGain(0.8)         OK\n";

    session.setDbapFocus(2.0f);
    std::cout << "[embedding_test]   setDbapFocus(2.0)          OK\n";

    session.setSpeakerMixDb(-3.0f);
    std::cout << "[embedding_test]   setSpeakerMixDb(-3.0 dB)   OK\n";

    session.setSubMixDb(0.0f);
    std::cout << "[embedding_test]   setSubMixDb(0.0 dB)        OK\n";

    session.setAutoCompensation(false);
    std::cout << "[embedding_test]   setAutoCompensation(false) OK\n";

    session.setElevationMode(ElevationMode::RescaleFullSphere);
    std::cout << "[embedding_test]   setElevationMode(RescaleFullSphere) OK\n\n";

    // ── queryStatus and consumeDiagnostics ────────────────────────────────
    EngineStatus status = session.queryStatus();
    std::cout << "[embedding_test] queryStatus:\n";
    std::cout << "[embedding_test]   timeSec=" << status.timeSec
              << "  cpuLoad=" << status.cpuLoad
              << "  paused=" << status.paused
              << "  isExitRequested=" << status.isExitRequested << "\n";

    DiagnosticEvents ev = session.consumeDiagnostics();
    std::cout << "[embedding_test] consumeDiagnostics: "
              << "renderRelocEvent=" << ev.renderRelocEvent
              << "  deviceRelocEvent=" << ev.deviceRelocEvent << "\n\n";

    // ── shutdown ──────────────────────────────────────────────────────────
    session.shutdown();
    std::cout << "[embedding_test] shutdown: OK\n";
    std::cout << "[embedding_test] PASS — EngineSessionCore links and full API surface callable\n";

    return 0;
}
