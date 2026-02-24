// main.cpp — Real-Time Spatial Audio Engine entry point
//
// This is the CLI entry point for the real-time engine. It:
//   1. Parses command-line arguments (layout, scene, sources, etc.)
//   2. Creates the RealtimeConfig and EngineState
//   3. Initializes the Backend Adapter (AlloLib AudioIO)
//   4. Starts audio streaming
//   5. Runs a monitoring loop until interrupted (Ctrl+C or scene end)
//   6. Shuts down cleanly
//
// PHASE 1: Outputs silence. Validates that the audio device opens and streams.
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
    std::cout << "\nsonoPleth Real-Time Spatial Audio Engine (Phase 1 — silence test)\n"
              << "─────────────────────────────────────────────────────────────────\n"
              << "Usage: " << progName << " [options]\n\n"
              << "Required:\n"
              << "  --layout <path>     Speaker layout JSON file\n"
              << "  --scene <path>      LUSID scene JSON file (positions/trajectories)\n"
              << "  --sources <path>    Folder containing mono source WAV files\n\n"
              << "Optional:\n"
              << "  --samplerate <int>  Audio sample rate in Hz (default: 48000)\n"
              << "  --buffersize <int>  Frames per audio callback (default: 512)\n"
              << "  --channels <int>    Number of output channels (default: 60)\n"
              << "  --gain <float>      Master gain 0.0–1.0 (default: 0.5)\n"
              << "  --help              Show this message\n"
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
    std::cout << "║   sonoPleth Real-Time Spatial Audio Engine  (Phase 1)   ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════╝\n" << std::endl;

    // ── Parse arguments ──────────────────────────────────────────────────

    RealtimeConfig config;
    EngineState    state;

    config.layoutPath    = getArgString(argc, argv, "--layout");
    config.scenePath     = getArgString(argc, argv, "--scene");
    config.sourcesFolder = getArgString(argc, argv, "--sources");
    config.sampleRate    = getArgInt(argc, argv, "--samplerate", 48000);
    config.bufferSize    = getArgInt(argc, argv, "--buffersize", 512);
    config.outputChannels = getArgInt(argc, argv, "--channels", 60);
    config.masterGain.store(getArgFloat(argc, argv, "--gain", 0.5f));

    // ── Validate required arguments ──────────────────────────────────────
    // Phase 1 doesn't actually load files, but we still validate the args
    // are provided so the CLI contract is established early.

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
    std::cout << "  Channels:     " << config.outputChannels << std::endl;
    std::cout << "  Master gain:  " << config.masterGain.load() << std::endl;
    std::cout << std::endl;

    // ── Register signal handler for clean Ctrl+C shutdown ────────────────
    g_config = &config;
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // ── Phase 1: Initialize and start the Backend Adapter ────────────────

    RealtimeBackend backend(config, state);

    if (!backend.init()) {
        std::cerr << "[Main] FATAL: Backend initialization failed." << std::endl;
        return 1;
    }

    if (!backend.start()) {
        std::cerr << "[Main] FATAL: Backend failed to start." << std::endl;
        return 1;
    }

    // ── Monitoring loop ──────────────────────────────────────────────────
    // Run until Ctrl+C or shouldExit is set. Print status every second.
    // This is where the GUI event loop would go in the future.

    std::cout << "[Main] Audio streaming. Press Ctrl+C to stop.\n" << std::endl;

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
                  << "  |  Frames: " << state.frameCounter.load(std::memory_order_relaxed)
                  << "     " << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << std::endl;

    // ── Clean shutdown ───────────────────────────────────────────────────

    std::cout << "\n[Main] Shutting down..." << std::endl;
    backend.shutdown();

    std::cout << "[Main] Final stats:" << std::endl;
    std::cout << "  Total frames: " << state.frameCounter.load() << std::endl;
    std::cout << "  Total time:   " << state.playbackTimeSec.load() << " seconds" << std::endl;
    std::cout << "[Main] Goodbye." << std::endl;

    return 0;
}

