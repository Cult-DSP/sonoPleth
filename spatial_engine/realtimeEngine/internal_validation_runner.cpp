// internal_validation_runner.cpp
#include "EngineSession.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    spatial::EngineSession session;

    // 1. Configure Engine Resource Allocation
    spatial::EngineConfig eCfg;
    eCfg.sampleRate = 48000;
    eCfg.bufferSize = 512;
    if (!session.configureEngine(eCfg)) {
        std::cerr << "Engine Fail: " << session.getLastError() << "\n";
        return 1;
    }

    // 2. Load Audio Scene
    spatial::SceneConfig sCfg{"path/to/scene.lusid.json"};
    if (!session.loadScene(sCfg)) return 1;

    // 3. Apply Speaker Layout
    spatial::LayoutConfig lCfg{"layout_7_1_4.json"};
    if (!session.applyLayout(lCfg)) return 1;

    // 4. Bind Runtime/OSC
    spatial::RuntimeConfig rCfg{ /* active port, telemetry toggles */ };
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
        
        for (const auto& msg : diagnostics) {
            std::cout << "Engine Warning: " << msg.text << "\n";
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
