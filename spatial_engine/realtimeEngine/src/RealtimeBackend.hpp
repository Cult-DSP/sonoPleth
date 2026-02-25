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
// - Phase 1: outputs silence.
// - Phase 2: reads audio from StreamingAgent and outputs a mono mix to all
//   channels (verifies streaming works). Full DBAP spatialization in Phase 4.
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
#include <cstring>  // memset, memcpy
#include <vector>
#include <algorithm> // std::min

#include "al/io/al_AudioIO.hpp"

#include "RealtimeTypes.hpp"
#include "StreamingAgent.hpp"  // StreamingAgent — needed for inline processBlock()

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

    // ── Agent wiring ─────────────────────────────────────────────────────
    //
    // The backend holds a raw pointer to the StreamingAgent. Ownership
    // stays with main(). The pointer is set once before start() and never
    // changes during audio streaming — no synchronization needed.

    /// Connect the streaming agent. Must be called BEFORE start().
    /// Also caches the source names so the audio callback doesn't need
    /// to query the map on every block.
    void setStreamingAgent(StreamingAgent* agent) {
        mStreamer = agent;
    }

    /// Cache source names from the streaming agent for use in processBlock().
    /// Must be called AFTER loadScene() and BEFORE start().
    void cacheSourceNames(const std::vector<std::string>& names) {
        mSourceNames = names;
        // Pre-allocate the mono mix buffer for the largest possible block.
        // This avoids allocation on the audio thread.
        mMonoMixBuffer.resize(mConfig.bufferSize, 0.0f);
        std::cout << "[Backend] Cached " << mSourceNames.size()
                  << " source names for audio callback." << std::endl;
    }


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
    // Phase 2: reads mono audio from each source via StreamingAgent, sums
    //   all sources into a mono mix, applies master gain, and copies to
    //   all output channels (flat mono output for streaming verification).
    //
    // Future phases will insert the full processing chain here:
    //   3. Pose agent → computes per-source positions for this block
    //   4. Spatializer agent → DBAP panning → writes to output channels
    //   5. LFE router → routes LFE-tagged sources to subwoofer channels
    //   6. Compensation & gain → per-channel trim
    //   7. Output remap → maps logical channels to physical device channels
    //
    // REAL-TIME CONTRACT:
    // - No allocation, no locks, no I/O.
    // - Only reads from pre-filled streaming buffers (lock-free).

    void processBlock(al::AudioIOData& io) {

        const unsigned int numFrames   = io.framesPerBuffer();
        const unsigned int numChannels = io.channelsOut();

        // ── Step 1: Zero all output channels ─────────────────────────────
        for (unsigned int ch = 0; ch < numChannels; ++ch) {
            float* buf = io.outBuffer(ch);
            std::memset(buf, 0, numFrames * sizeof(float));
        }

        // ── Step 2: Read and mix all sources ─────────────────────────────
        // For each source, get a mono block from the streaming agent, then
        // accumulate into the first output channel. This is a simple sum
        // (no spatialization) — just proves the streaming pipeline works.
        //
        // In Phase 4, this becomes per-source DBAP → per-speaker channels.

        if (mStreamer && !mSourceNames.empty()) {
            uint64_t currentFrame = mState.frameCounter.load(std::memory_order_relaxed);
            float masterGain = mConfig.masterGain.load(std::memory_order_relaxed);

            // Scale factor to prevent clipping when summing many sources.
            // Simple 1/N normalization — sufficient for testing.
            float sourceScale = 1.0f / static_cast<float>(mSourceNames.size());
            float gainPerSource = masterGain * sourceScale;

            float* outCh0 = io.outBuffer(0);

            for (const auto& name : mSourceNames) {
                // Fill the pre-allocated mono buffer with this source's audio
                mStreamer->getBlock(name, currentFrame, numFrames,
                                    mMonoMixBuffer.data());

                // Accumulate into output channel 0
                for (unsigned int f = 0; f < numFrames; ++f) {
                    outCh0[f] += mMonoMixBuffer[f] * gainPerSource;
                }
            }

            // Copy channel 0 to all other channels (mono mirror for testing)
            // This lets us hear the mix on any connected speaker/headphone.
            for (unsigned int ch = 1; ch < numChannels; ++ch) {
                float* dest = io.outBuffer(ch);
                std::memcpy(dest, outCh0, numFrames * sizeof(float));
            }
        }

        // ── Step 3: Update engine state ──────────────────────────────────
        uint64_t prevFrames = mState.frameCounter.load(std::memory_order_relaxed);
        uint64_t newFrames  = prevFrames + numFrames;
        mState.frameCounter.store(newFrames, std::memory_order_relaxed);
        mState.playbackTimeSec.store(
            static_cast<double>(newFrames) / mConfig.sampleRate,
            std::memory_order_relaxed
        );

        // ── Step 4: CPU load monitoring ──────────────────────────────────
        float rawCpu = static_cast<float>(mAudioIO.cpu());
        float clampedCpu = (rawCpu < 0.0f) ? 0.0f : (rawCpu > 1.0f ? 1.0f : rawCpu);
        mState.cpuLoad.store(clampedCpu, std::memory_order_relaxed);
    }

    // ── Member data ──────────────────────────────────────────────────────

    RealtimeConfig& mConfig;    // Reference to shared config (set at startup)
    EngineState&    mState;     // Reference to shared engine state
    al::AudioIO     mAudioIO;   // AlloLib audio device wrapper
    bool            mInitialized = false;

    // ── Agent pointers (set once before start(), never changed) ──────────
    StreamingAgent* mStreamer = nullptr;

    // ── Cached data for audio callback (set once, read-only in callback) ─
    std::vector<std::string> mSourceNames;  // Source name list for iteration
    std::vector<float>       mMonoMixBuffer; // Pre-allocated per-source temp buffer
};

