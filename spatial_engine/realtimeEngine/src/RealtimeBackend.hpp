// RealtimeBackend.hpp — Agent 8: Audio Backend Adapter
//
// Wraps AlloLib's AudioIO to provide a clean interface between the real-time
// engine and the audio hardware. This is the ONLY file that should directly
// touch AudioIO — all other agents interact through the callback chain and
// shared types.
//
// RESPONSIBILITIES:
// 1. Initialize the audio device with the correct sample rate, buffer size,
//    and channel count from RealtimeConfig.
// 2. Register the top-level audio callback.
// 3. Start / stop the audio stream.
// 4. Report CPU load and detect xruns.
//
// DESIGN NOTES:
// - The callback function is static (required by AlloLib's C-style callback).
//   It receives `this` via the userData pointer and dispatches to the member
//   function `processBlock()`.
// - In Phase 1, processBlock() outputs silence. Future phases will insert
//   the streaming → spatializer → LFE → compensation → remap chain.
// - The callback must NEVER allocate, lock, or do I/O. It runs on the audio
//   thread at real-time priority.
//
// REFERENCE: AlloLib AudioIO API (thirdparty/allolib/include/al/io/al_AudioIO.hpp)
//   AudioIO::init(callback, userData, framesPerBuf, framesPerSec, outChans, inChans)
//   AudioIO::open() / start() / stop() / close()
//   AudioIO::cpu() → current audio thread CPU load
//   AudioIOData::out(chan, frame) → write to output buffer
//   AudioIOData::framesPerBuffer() → number of frames in current callback
//   AudioIOData::channelsOut() → number of output channels

#pragma once

#include <iostream>
#include <string>
#include <functional>
#include <cstring> // memset

#include "al/io/al_AudioIO.hpp"

#include "RealtimeTypes.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// RealtimeBackend — AlloLib AudioIO wrapper for the real-time engine
// ─────────────────────────────────────────────────────────────────────────────

class RealtimeBackend {
public:

    // ── Constructor / Destructor ─────────────────────────────────────────

    RealtimeBackend(RealtimeConfig& config, EngineState& state)
        : mConfig(config), mState(state) {}

    ~RealtimeBackend() {
        shutdown();
    }

    // ── Lifecycle ────────────────────────────────────────────────────────

    /// Initialize the audio device. Must be called before start().
    /// Returns true on success.
    bool init() {
        std::cout << "[Backend] Initializing audio device..." << std::endl;
        std::cout << "  Sample rate:      " << mConfig.sampleRate << " Hz" << std::endl;
        std::cout << "  Buffer size:      " << mConfig.bufferSize << " frames" << std::endl;
        std::cout << "  Output channels:  " << mConfig.outputChannels << std::endl;
        std::cout << "  Input channels:   " << mConfig.inputChannels << std::endl;

        // Register the static callback with 'this' as userData so we can
        // dispatch into the member function processBlock().
        mAudioIO.init(
            audioCallback,              // static callback function
            this,                       // userData → passed back in callback
            mConfig.bufferSize,         // frames per buffer
            (double)mConfig.sampleRate, // sample rate
            mConfig.outputChannels,     // output channels
            mConfig.inputChannels       // input channels
        );

        // Open the device (allocates hardware buffers)
        if (!mAudioIO.open()) {
            std::cerr << "[Backend] ERROR: Failed to open audio device." << std::endl;
            return false;
        }

        mInitialized = true;
        std::cout << "[Backend] Audio device opened successfully." << std::endl;

        // Report actual device parameters (may differ from requested)
        std::cout << "  Actual output channels: " << mAudioIO.channelsOut() << std::endl;
        std::cout << "  Actual buffer size:     " << mAudioIO.framesPerBuffer() << std::endl;

        return true;
    }

    /// Start audio streaming. Returns true on success.
    bool start() {
        if (!mInitialized) {
            std::cerr << "[Backend] ERROR: Cannot start — not initialized." << std::endl;
            return false;
        }
        std::cout << "[Backend] Starting audio stream..." << std::endl;

        if (!mAudioIO.start()) {
            std::cerr << "[Backend] ERROR: Failed to start audio stream." << std::endl;
            return false;
        }

        mConfig.playing.store(true);
        std::cout << "[Backend] Audio stream started." << std::endl;
        return true;
    }

    /// Stop audio streaming.
    void stop() {
        if (mAudioIO.isRunning()) {
            std::cout << "[Backend] Stopping audio stream..." << std::endl;
            mAudioIO.stop();
            mConfig.playing.store(false);
            std::cout << "[Backend] Audio stream stopped." << std::endl;
        }
    }

    /// Full shutdown: stop stream and close device.
    void shutdown() {
        stop();
        if (mInitialized) {
            mAudioIO.close();
            mInitialized = false;
            std::cout << "[Backend] Audio device closed." << std::endl;
        }
    }

    // ── Status queries ───────────────────────────────────────────────────

    /// Current CPU load of the audio thread (0.0–1.0).
    double cpuLoad() const { return mAudioIO.cpu(); }

    /// Whether the audio stream is currently running.
    bool isRunning() { return mAudioIO.isRunning(); }

    /// Whether the device has been initialized.
    bool isInitialized() const { return mInitialized; }

    // ── Access to underlying AudioIO (for future agent chaining) ─────────

    /// Returns a reference to the AudioIO object.
    /// Used by agents that need to append their own AudioCallbacks.
    al::AudioIO& audioIO() { return mAudioIO; }


private:

    // ── Static audio callback (C-style, required by AlloLib) ─────────────
    //
    // AlloLib calls this on the audio thread. We recover 'this' from
    // userData and dispatch to the member function.

    static void audioCallback(al::AudioIOData& io) {
        RealtimeBackend* self = static_cast<RealtimeBackend*>(io.user());
        if (self) {
            self->processBlock(io);
        }
    }

    // ── Per-block processing (called on audio thread) ────────────────────
    //
    // Phase 1: outputs silence and updates engine state.
    // Future phases will insert the full processing chain here:
    //   1. Streaming agent → fills per-source mono buffers
    //   2. Pose agent → computes per-source positions for this block
    //   3. Spatializer agent → DBAP panning → writes to output channels
    //   4. LFE router → routes LFE-tagged sources to subwoofer channels
    //   5. Compensation & gain → applies master gain, per-channel trim
    //   6. Output remap → maps logical channels to physical device channels

    void processBlock(al::AudioIOData& io) {

        const unsigned int numFrames = io.framesPerBuffer();
        const unsigned int numChannels = io.channelsOut();

        // ── Phase 1: Silence output ──────────────────────────────────────
        // Zero all output channels. This is the baseline — future agents
        // will fill these buffers with spatialized audio.
        for (unsigned int ch = 0; ch < numChannels; ++ch) {
            float* buf = io.outBuffer(ch);
            std::memset(buf, 0, numFrames * sizeof(float));
        }

        // ── Update engine state ──────────────────────────────────────────
        // Advance the frame counter and playback time.
        uint64_t prevFrames = mState.frameCounter.load(std::memory_order_relaxed);
        uint64_t newFrames = prevFrames + numFrames;
        mState.frameCounter.store(newFrames, std::memory_order_relaxed);
        mState.playbackTimeSec.store(
            static_cast<double>(newFrames) / mConfig.sampleRate,
            std::memory_order_relaxed
        );

        // ── CPU load monitoring ──────────────────────────────────────────
        // AlloLib's cpu() returns the fraction of the buffer period used
        // by the callback. We clamp to [0, 1] because AlloLib can return
        // negative values when the callback is trivially fast.
        float rawCpu = static_cast<float>(mAudioIO.cpu());
        float clampedCpu = (rawCpu < 0.0f) ? 0.0f : (rawCpu > 1.0f ? 1.0f : rawCpu);
        mState.cpuLoad.store(clampedCpu, std::memory_order_relaxed);
    }

    // ── Member data ──────────────────────────────────────────────────────

    RealtimeConfig& mConfig;    // Reference to shared config (set at startup)
    EngineState&    mState;     // Reference to shared engine state
    al::AudioIO     mAudioIO;   // AlloLib audio device wrapper
    bool            mInitialized = false;
};

