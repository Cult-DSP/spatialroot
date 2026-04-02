// internal_validation_runner.cpp
#include "EngineSession.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    EngineSession session;

    // 1. Configure Engine Resource Allocation
    EngineOptions eCfg;
    eCfg.sampleRate = 48000;
    eCfg.bufferSize = 512;
    if (!session.configureEngine(eCfg)) {
        std::cerr << "Engine Fail: " << session.getLastError() << "\n";
        return 1;
    }

    // 2. Load Audio Scene
    SceneInput sCfg;
    sCfg.scenePath = "/Users/lucian/projects/spatialroot/sourceData/lusid_package/scene.lusid.json";
    sCfg.sourcesFolder = "/Users/lucian/projects/spatialroot/sourceData/lusid_package/";
    if (!session.loadScene(sCfg)) return 1;

    // 3. Apply Speaker Layout
    LayoutInput lCfg;
    lCfg.layoutPath = "/Users/lucian/projects/spatialroot/spatial_engine/speaker_layouts/stereo.json";
    if (!session.applyLayout(lCfg)) return 1;

    // 4. Bind Runtime/OSC
    RuntimeParams rCfg;
    if (!session.configureRuntime(rCfg)) return 1;

    // 5. Ignite Backend
    if (!session.start()) {
        std::cerr << "Start Fail: " << session.getLastError() << "\n";
        return 1;
    }

    // Main Thread Render Loop
    for (int i = 0; i < 600; ++i) { // 10 seconds at 60fps UI tick
        session.update(); // CRITICAL: processes focus compensation

        auto status = session.queryStatus();
        auto diagnostics = session.consumeDiagnostics();
        
        if (diagnostics.renderRelocEvent) {
            std::cout << "Engine Warning: Relocation Event\n";
        }

        // Test transport constraint
        if (i == 300) session.setPaused(true);
        if (i == 360) session.setPaused(false);

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // 6. Strict Teardown sequence triggered internally by shutdown()
    session.shutdown();
    return 0;
}
