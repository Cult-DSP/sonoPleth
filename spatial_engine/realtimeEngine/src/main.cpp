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
//   9. Runs a monitoring loop until interrupted (Ctrl+C or scene end)
//  10. Shuts down cleanly (streaming agent → backend)
//
// PHASE 3: Streams all sources and computes per-source positions at each
//   audio block. Positions are computed via SLERP interpolation and
//   layout-aware transforms but are not yet used for spatialization.
//   Audio output is still the flat mono mix from Phase 2.
//
// Usage:
//   ./sonoPleth_realtime_engine \
//       --layout ../speaker_layouts/allosphere_layout.json \
//       --scene ../../processedData/stageForRender/scene.lusid.json \
//       --sources ../../sourceData/lusid_package \
//       [--samplerate 48000] \
//       [--buffersize 512] \
//       [--channels 60] \
//       [--gain 0.5]

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
#include "Spatializer.hpp"     // Spatializer — DBAP spatial panning
#include "JSONLoader.hpp"      // SpatialData, JSONLoader::loadLusidScene()
#include "LayoutLoader.hpp"    // SpeakerLayoutData, LayoutLoader::loadLayout()

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
    std::cout << "\nsonoPleth Real-Time Spatial Audio Engine (Phase 4 — DBAP spatialization)\n"
              << "─────────────────────────────────────────────────────────────────────\n"
              << "Usage: " << progName << " [options]\n\n"
              << "Required:\n"
              << "  --layout <path>     Speaker layout JSON file\n"
              << "  --scene <path>      LUSID scene JSON file (positions/trajectories)\n"
              << "  --sources <path>    Folder containing mono source WAV files\n\n"
              << "Optional:\n"
              << "  --samplerate <int>  Audio sample rate in Hz (default: 48000)\n"
              << "  --buffersize <int>  Frames per audio callback (default: 512)\n"
              << "  --gain <float>      Master gain 0.0–1.0 (default: 0.5)\n"
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
    std::cout << "║   sonoPleth Real-Time Spatial Audio Engine  (Phase 4)   ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════╝\n" << std::endl;

    // ── Parse arguments ──────────────────────────────────────────────────

    RealtimeConfig config;
    EngineState    state;

    config.layoutPath    = getArgString(argc, argv, "--layout");
    config.scenePath     = getArgString(argc, argv, "--scene");
    config.sourcesFolder = getArgString(argc, argv, "--sources");
    config.sampleRate    = getArgInt(argc, argv, "--samplerate", 48000);
    config.bufferSize    = getArgInt(argc, argv, "--buffersize", 512);
    config.masterGain.store(getArgFloat(argc, argv, "--gain", 0.5f));
    // NOTE: outputChannels is computed from the speaker layout (see Spatializer::init).
    //       No --channels flag needed.

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
    if (config.sourcesFolder.empty()) {
        std::cerr << "[Main] ERROR: --sources is required." << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    std::cout << "[Main] Configuration:" << std::endl;
    std::cout << "  Layout:       " << config.layoutPath << std::endl;
    std::cout << "  Scene:        " << config.scenePath << std::endl;
    std::cout << "  Sources:      " << config.sourcesFolder << std::endl;
    std::cout << "  Sample rate:  " << config.sampleRate << " Hz" << std::endl;
    std::cout << "  Buffer size:  " << config.bufferSize << " frames" << std::endl;
    std::cout << "  Master gain:  " << config.masterGain.load() << std::endl;
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

    // Create the streaming agent and load all source WAVs
    Streaming streaming(config, state);
    if (!streaming.loadScene(scene)) {
        std::cerr << "[Main] FATAL: No source files could be loaded." << std::endl;
        return 1;
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
              << " speakers, focus=" << config.dbapFocus << "." << std::endl;
    std::cout << "[Main] Output channels (from layout): " << config.outputChannels << std::endl;

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

    while (!config.shouldExit.load()) {

        // Print status every second
        double timeSec = state.playbackTimeSec.load(std::memory_order_relaxed);
        float  cpu     = state.cpuLoad.load(std::memory_order_relaxed);

        std::cout << "\r  Time: " << std::fixed;
        std::cout.precision(1);
        std::cout << timeSec << "s"
                  << "  |  CPU: ";
        std::cout.precision(1);
        std::cout << (cpu * 100.0f) << "%"
                  << "  |  Sources: " << state.numSources.load(std::memory_order_relaxed)
                  << "  |  Frames: " << state.frameCounter.load(std::memory_order_relaxed)
                  << "     " << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << std::endl;

    // ── Clean shutdown ───────────────────────────────────────────────────
    // Order matters: stop audio first, then streaming agent, so the callback
    // doesn't try to read from freed buffers.

    std::cout << "\n[Main] Shutting down..." << std::endl;
    backend.shutdown();
    streaming.shutdown();

    std::cout << "[Main] Final stats:" << std::endl;
    std::cout << "  Total frames: " << state.frameCounter.load() << std::endl;
    std::cout << "  Total time:   " << state.playbackTimeSec.load() << " seconds" << std::endl;
    std::cout << "[Main] Goodbye." << std::endl;

    return 0;
}

