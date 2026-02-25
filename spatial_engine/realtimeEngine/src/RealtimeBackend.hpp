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
// - Phase 2: reads audio from Streaming and outputs a mono mix to all
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
#include "Streaming.hpp"      // Streaming — needed for inline processBlock()
#include "Pose.hpp"           // Pose — needed for inline processBlock()
#include "Spatializer.hpp"    // Spatializer — needed for inline processBlock()

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
    // The backend holds raw pointers to agents. Ownership stays with main().
    // Pointers are set once before start() and never change during audio
    // streaming — no synchronization needed.

    /// Connect the streaming agent. Must be called BEFORE start().
    void setStreaming(Streaming* agent) {
        mStreamer = agent;
    }

    /// Connect the pose agent. Must be called BEFORE start().
    void setPose(Pose* agent) {
        mPose = agent;
    }

    /// Connect the spatializer agent. Must be called BEFORE start().
    void setSpatializer(Spatializer* agent) {
        mSpatializer = agent;
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
    // Full spatial rendering pipeline (all phases integrated):
    //   1. Zero output buffers
    //   2. Pose agent computes per-source positions (SLERP + layout transform)
    //   3. Spatializer distributes each source via DBAP across speakers;
    //      LFE sources are routed directly to subwoofer channels (Phase 2/4);
    //      loudspeaker/sub mix trims applied after DBAP (Phase 6);
    //      output channel remap applied before copy-to-device (Phase 7).
    //   4. Update EngineState frame counter + playback time
    //   5. CPU load monitoring
    //
    // THREADING:  All code here runs on the AUDIO THREAD exclusively.
    //             See RealtimeTypes.hpp for the full threading model.
    //
    // REAL-TIME CONTRACT:
    // - No allocation, no locks, no I/O.
    // - Streaming read is lock-free (double-buffered, atomic state flags).
    // - Pose read is single-threaded (audio thread owns mPoses/mLastGoodDir).
    // - All EngineState writes use memory_order_relaxed (single writer here;
    //   main/loader threads only poll for display, one-buffer lag is fine).

    void processBlock(al::AudioIOData& io) {

        const unsigned int numFrames   = io.framesPerBuffer();
        const unsigned int numChannels = io.channelsOut();

        // ── Step 1: Zero all output channels ─────────────────────────────
        for (unsigned int ch = 0; ch < numChannels; ++ch) {
            float* buf = io.outBuffer(ch);
            std::memset(buf, 0, numFrames * sizeof(float));
        }

        // ── Step 2: Compute source positions for this block ──────────────
        // Guard: mPose must be non-null (same guard as Step 3 for consistency).
        if (mPose) {
            uint64_t curFrame = mState.frameCounter.load(std::memory_order_relaxed);
            double blockCenterSec = static_cast<double>(curFrame + numFrames / 2)
                                    / mConfig.sampleRate;
            mPose->computePositions(blockCenterSec);
        }

        // ── Step 3: Spatialize all sources via DBAP ──────────────────────
        // The Spatializer reads each source's audio from Streaming, gets
        // its position from Pose, and distributes it across speakers.
        // LFE sources are routed directly to subwoofer channels.
        // Requires all three agents to be wired; silently outputs nothing if any
        // pointer is null (should never happen in production after init()).
        if (mSpatializer && mStreamer && mPose) {
            uint64_t currentFrame = mState.frameCounter.load(std::memory_order_relaxed);
            const auto& poses = mPose->getPoses();
            mSpatializer->renderBlock(io, *mStreamer, poses,
                                       currentFrame, numFrames);
        }

        // ── Step 4: Update engine state ──────────────────────────────────
        // memory_order_relaxed is correct: we are the sole writer; main thread
        // reads these only for display and does not derive safety decisions from
        // their exact value relative to any other memory operation.
        uint64_t prevFrames = mState.frameCounter.load(std::memory_order_relaxed);
        uint64_t newFrames  = prevFrames + numFrames;
        mState.frameCounter.store(newFrames, std::memory_order_relaxed);
        mState.playbackTimeSec.store(
            static_cast<double>(newFrames) / mConfig.sampleRate,
            std::memory_order_relaxed
        );

        // ── Step 5: CPU load monitoring ──────────────────────────────────
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
    // THREADING: Set on the MAIN thread before start(). After start() these
    // are read-only on the AUDIO thread. No synchronization needed —
    // start() provides the required happens-before relationship.
    Streaming*    mStreamer     = nullptr;
    Pose*         mPose         = nullptr;
    Spatializer*  mSpatializer  = nullptr;

    // ── Cached data for audio callback (set once, read-only in callback) ─
    // THREADING: Written by main thread (cacheSourceNames) before start(),
    // then only read on the audio thread. Same happens-before as agent ptrs.
    std::vector<std::string> mSourceNames;   // Source name list (reserved for future use)
    std::vector<float>       mMonoMixBuffer; // Pre-allocated temp buffer (reserved for future use)
};

