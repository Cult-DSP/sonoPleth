// Streaming.hpp — Agent 1: Audio Streaming from Disk
//
// Streams mono WAV source files from disk in real-time using double-buffered
// I/O. Each source gets two pre-allocated buffers that alternate: one is read
// by the audio thread while the other is filled by a background loader thread.
//
// RESPONSIBILITIES:
// 1. Open all source WAV files referenced in the LUSID scene.
// 2. Pre-allocate double buffers for each source at load time.
// 3. Run a background thread that reads ahead into the inactive buffer.
// 4. Provide a lock-free getSamples() method for the audio callback.
// 5. Handle end-of-file (output silence after source ends).
//
// REAL-TIME SAFETY:
// - The audio callback (getSamples) NEVER does file I/O, locks, or allocates.
// - It reads from a pre-filled buffer and uses atomic state flags.
// - The background loader thread is the ONLY thread that touches libsndfile.
// - A mutex protects SNDFILE* access (only used by the loader thread).
//
// DEPENDENCY:
// - <sndfile.h> comes transitively through Gamma (AlloLib external).
//   Gamma's CMake does: find_package(LibSndFile QUIET) and exports via PUBLIC.
//   Same API that spatial_engine/src/WavUtils.cpp already uses.
//   No new dependencies introduced.
//
// PROVENANCE:
// - Double-buffer pattern adapted from mainplayer.hpp prototype
//   (internalDocsMD/realtime_planning/mainplayer.hpp)
// - File loading pattern adapted from WavUtils.cpp (spatial_engine/src/)
// - Design follows realtimeEngine_designDoc.md §Streaming Module

#pragma once

#include <atomic>
#include <cstring>    // memset, memcpy
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>

#include <sndfile.h>  // via Gamma (AlloLib external) — NOT a new dependency

#include "RealtimeTypes.hpp"
#include "JSONLoader.hpp"  // SpatialData, Keyframe — shared from spatial_engine/src/
#include "MultichannelReader.hpp"  // ADM direct streaming — multichannel reader

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

// Chunk size in frames for each double buffer.
// Each buffer holds this many mono float samples.
// 5 seconds at 48kHz = 240,000 frames = ~940 KB per source.
// For 80 sources: ~75 MB total buffer memory (2 buffers × 80 sources).
// This is a good balance between memory usage and preload safety margin.
static constexpr uint64_t kDefaultChunkFrames = 48000 * 5;  // 5 seconds

// When playback reaches this fraction of the current chunk, trigger preload
// of the next chunk into the inactive buffer.
static constexpr float kPreloadThreshold = 0.5f;  // Start loading at 50%

// ─────────────────────────────────────────────────────────────────────────────
// BufferState — State machine for each double buffer slot
// ─────────────────────────────────────────────────────────────────────────────
// Transitions:
//   EMPTY → LOADING (loader thread starts filling)
//   LOADING → READY (loader thread finished filling)
//   READY → PLAYING (audio thread switched to this buffer)
//   PLAYING → EMPTY (audio thread finished with this buffer, moved to other)

enum class StreamBufferState : int {
    EMPTY   = 0,  // Buffer is empty / available for loading
    LOADING = 1,  // Loader thread is filling this buffer
    READY   = 2,  // Buffer is filled and ready for audio thread
    PLAYING = 3   // Audio thread is currently reading from this buffer
};

// ─────────────────────────────────────────────────────────────────────────────
// SourceStream — Per-source streaming state
// ─────────────────────────────────────────────────────────────────────────────
// Each audio source (e.g., "1.1", "11.1", "LFE") gets one of these.
// Contains the SNDFILE handle, double buffers, and playback cursor.

struct SourceStream {
    // ── Identity ─────────────────────────────────────────────────────────
    std::string name;           // Source key (e.g., "1.1", "LFE")
    std::string filePath;       // Full path to the WAV file

    // ── File handle (only accessed by loader thread, protected by mutex) ─
    SNDFILE*    sndFile = nullptr;
    SF_INFO     sfInfo  = {};
    std::mutex  fileMutex;      // Protects sndFile seek/read operations

    // ── Double buffers ───────────────────────────────────────────────────
    // Two pre-allocated float buffers. Each holds up to chunkFrames samples.
    std::vector<float> bufferA;
    std::vector<float> bufferB;

    // Atomic state for each buffer (lock-free coordination)
    // Marked mutable because the audio thread may switch the active buffer
    // during a logically-const getSample() call (buffer switch doesn't
    // modify contents, just which buffer is "current").
    mutable std::atomic<StreamBufferState> stateA{StreamBufferState::EMPTY};
    mutable std::atomic<StreamBufferState> stateB{StreamBufferState::EMPTY};

    // The frame offset (in the source file) where each buffer's data starts
    std::atomic<uint64_t> chunkStartA{0};
    std::atomic<uint64_t> chunkStartB{0};

    // How many valid frames are actually in each buffer
    // (may be < chunkFrames for the last chunk in the file)
    std::atomic<uint64_t> validFramesA{0};
    std::atomic<uint64_t> validFramesB{0};

    // Which buffer is currently active for playback (0 = A, 1 = B)
    // Mutable for same reason as stateA/stateB above.
    mutable std::atomic<int> activeBuffer{-1};  // -1 = no buffer active yet

    // ── Playback state ───────────────────────────────────────────────────
    uint64_t totalFrames = 0;     // Total frames in the source WAV
    int      sampleRate  = 0;     // Source sample rate (must match engine)
    bool     isLFE       = false; // True if this is the LFE source

    // ── Buffer sizing ────────────────────────────────────────────────────
    uint64_t chunkFrames = kDefaultChunkFrames;

    // ── Methods ──────────────────────────────────────────────────────────

    /// Open the WAV file and pre-allocate buffers. Called once at load time.
    /// Returns true on success.
    bool open(const std::string& path, const std::string& sourceName,
              uint64_t chunkSize, int expectedSR) {
        name = sourceName;
        filePath = path;
        chunkFrames = chunkSize;
        isLFE = (sourceName == "LFE");

        // Open the file (header only — no data loaded into memory)
        sfInfo = {};
        sndFile = sf_open(path.c_str(), SFM_READ, &sfInfo);
        if (!sndFile) {
            std::cerr << "[Streaming] ERROR: Cannot open WAV: " << path
                      << " — " << sf_strerror(nullptr) << std::endl;
            return false;
        }

        // Validate: must be mono
        if (sfInfo.channels != 1) {
            std::cerr << "[Streaming] ERROR: Source is not mono (" 
                      << sfInfo.channels << " ch): " << path << std::endl;
            sf_close(sndFile);
            sndFile = nullptr;
            return false;
        }

        // Validate: sample rate must match engine
        if (sfInfo.samplerate != expectedSR) {
            std::cerr << "[Streaming] ERROR: Sample rate mismatch in " << path
                      << " (got " << sfInfo.samplerate << ", expected " 
                      << expectedSR << ")" << std::endl;
            sf_close(sndFile);
            sndFile = nullptr;
            return false;
        }

        totalFrames = static_cast<uint64_t>(sfInfo.frames);
        sampleRate = sfInfo.samplerate;

        // Pre-allocate double buffers (no allocation during playback!)
        bufferA.resize(chunkFrames, 0.0f);
        bufferB.resize(chunkFrames, 0.0f);

        return true;
    }

    /// Initialize buffers WITHOUT opening a file handle. Used in multichannel
    /// (ADM direct) mode where MultichannelReader owns the file and fills
    /// these buffers via de-interleaving. The SourceStream still provides
    /// double-buffered playback to the audio thread exactly as in mono mode.
    bool initBuffersOnly(const std::string& sourceName, uint64_t chunkSize,
                         int sr, uint64_t frames) {
        name = sourceName;
        chunkFrames = chunkSize;
        isLFE = (sourceName == "LFE");
        totalFrames = frames;
        sampleRate = sr;

        // No file handle — MultichannelReader owns the SNDFILE*
        sndFile = nullptr;

        // Pre-allocate double buffers
        bufferA.resize(chunkFrames, 0.0f);
        bufferB.resize(chunkFrames, 0.0f);

        return true;
    }

    /// Load the first chunk synchronously into buffer A. Called once before
    /// playback starts (from the main thread, not the audio thread).
    bool loadFirstChunk() {
        if (!sndFile) return false;

        stateA.store(StreamBufferState::LOADING, std::memory_order_release);

        uint64_t framesToRead = std::min(chunkFrames, totalFrames);

        std::lock_guard<std::mutex> lock(fileMutex);
        sf_seek(sndFile, 0, SEEK_SET);
        sf_count_t read = sf_readf_float(sndFile, bufferA.data(),
                                          static_cast<sf_count_t>(framesToRead));

        if (read <= 0) {
            std::cerr << "[Streaming] ERROR: Failed to read first chunk for "
                      << name << std::endl;
            stateA.store(StreamBufferState::EMPTY, std::memory_order_release);
            return false;
        }

        // Zero-fill remainder if we read less than the chunk size
        if (static_cast<uint64_t>(read) < chunkFrames) {
            std::memset(bufferA.data() + read, 0,
                        (chunkFrames - read) * sizeof(float));
        }

        chunkStartA.store(0, std::memory_order_release);
        validFramesA.store(static_cast<uint64_t>(read), std::memory_order_release);
        stateA.store(StreamBufferState::READY, std::memory_order_release);

        // Activate buffer A for playback
        activeBuffer.store(0, std::memory_order_release);
        stateA.store(StreamBufferState::PLAYING, std::memory_order_release);

        return true;
    }

    /// Load a chunk starting at fileFrame into the specified buffer.
    /// Called ONLY by the loader thread.
    void loadChunkInto(int bufIdx, uint64_t fileFrame) {
        auto& buffer = (bufIdx == 0) ? bufferA : bufferB;
        auto& state  = (bufIdx == 0) ? stateA  : stateB;
        auto& start  = (bufIdx == 0) ? chunkStartA : chunkStartB;
        auto& valid  = (bufIdx == 0) ? validFramesA : validFramesB;

        state.store(StreamBufferState::LOADING, std::memory_order_release);

        // Clamp to file end
        uint64_t framesToRead = chunkFrames;
        if (fileFrame + framesToRead > totalFrames) {
            framesToRead = (fileFrame < totalFrames) ? (totalFrames - fileFrame) : 0;
        }

        if (framesToRead == 0) {
            // Past end of file — fill with silence
            std::memset(buffer.data(), 0, chunkFrames * sizeof(float));
            start.store(fileFrame, std::memory_order_release);
            valid.store(0, std::memory_order_release);
            state.store(StreamBufferState::READY, std::memory_order_release);
            return;
        }

        sf_count_t read = 0;
        {
            std::lock_guard<std::mutex> lock(fileMutex);
            sf_seek(sndFile, static_cast<sf_count_t>(fileFrame), SEEK_SET);
            read = sf_readf_float(sndFile, buffer.data(),
                                   static_cast<sf_count_t>(framesToRead));
        }

        // Zero-fill remainder
        if (static_cast<uint64_t>(read) < chunkFrames) {
            std::memset(buffer.data() + read, 0,
                        (chunkFrames - read) * sizeof(float));
        }

        start.store(fileFrame, std::memory_order_release);
        valid.store(static_cast<uint64_t>(read), std::memory_order_release);
        state.store(StreamBufferState::READY, std::memory_order_release);
    }

    /// Get the sample value at a given global frame position.
    /// Called ONLY from the audio callback thread — must be lock-free.
    /// Returns 0.0f if the frame is not in any loaded buffer (underrun).
    float getSample(uint64_t globalFrame) const {
        int active = activeBuffer.load(std::memory_order_acquire);
        if (active < 0) return 0.0f;  // No buffer active yet

        // Get active buffer's data
        const auto& buffer = (active == 0) ? bufferA : bufferB;
        uint64_t bufStart  = (active == 0) 
            ? chunkStartA.load(std::memory_order_acquire)
            : chunkStartB.load(std::memory_order_acquire);
        uint64_t bufValid  = (active == 0)
            ? validFramesA.load(std::memory_order_acquire)
            : validFramesB.load(std::memory_order_acquire);

        // Check if the requested frame is within this buffer
        if (globalFrame >= bufStart && globalFrame < bufStart + bufValid) {
            return buffer[globalFrame - bufStart];
        }

        // Frame not in active buffer — check the other buffer
        int other = 1 - active;
        const auto& otherBuf = (other == 0) ? bufferA : bufferB;
        auto otherState = (other == 0) 
            ? stateA.load(std::memory_order_acquire)
            : stateB.load(std::memory_order_acquire);
        uint64_t otherStart = (other == 0)
            ? chunkStartA.load(std::memory_order_acquire)
            : chunkStartB.load(std::memory_order_acquire);
        uint64_t otherValid = (other == 0)
            ? validFramesA.load(std::memory_order_acquire)
            : validFramesB.load(std::memory_order_acquire);

        if (otherState == StreamBufferState::READY &&
            globalFrame >= otherStart && globalFrame < otherStart + otherValid) {
            // The other buffer has our data — switch to it!
            // (This is a benign race: worst case two blocks both switch,
            //  but the data is consistent either way.)
            // stateA/stateB/activeBuffer are mutable, so this works in const.
            auto& mutState  = (active == 0) ? stateA : stateB;
            auto& othState  = (other == 0)  ? stateA : stateB;

            mutState.store(StreamBufferState::EMPTY, std::memory_order_release);
            othState.store(StreamBufferState::PLAYING, std::memory_order_release);
            activeBuffer.store(other, std::memory_order_release);

            return otherBuf[globalFrame - otherStart];
        }

        // Neither buffer has the data — underrun (return silence)
        return 0.0f;
    }

    /// Close the file handle. Called at shutdown.
    void close() {
        if (sndFile) {
            sf_close(sndFile);
            sndFile = nullptr;
        }
    }

    ~SourceStream() { close(); }

    // Non-copyable (owns SNDFILE handle)
    SourceStream() = default;
    SourceStream(const SourceStream&) = delete;
    SourceStream& operator=(const SourceStream&) = delete;
    SourceStream(SourceStream&& other) noexcept {
        name = std::move(other.name);
        filePath = std::move(other.filePath);
        sndFile = other.sndFile;  other.sndFile = nullptr;
        sfInfo = other.sfInfo;
        bufferA = std::move(other.bufferA);
        bufferB = std::move(other.bufferB);
        stateA.store(other.stateA.load());
        stateB.store(other.stateB.load());
        chunkStartA.store(other.chunkStartA.load());
        chunkStartB.store(other.chunkStartB.load());
        validFramesA.store(other.validFramesA.load());
        validFramesB.store(other.validFramesB.load());
        activeBuffer.store(other.activeBuffer.load());
        totalFrames = other.totalFrames;
        sampleRate = other.sampleRate;
        isLFE = other.isLFE;
        chunkFrames = other.chunkFrames;
    }
    SourceStream& operator=(SourceStream&& other) noexcept {
        if (this != &other) {
            close();
            name = std::move(other.name);
            filePath = std::move(other.filePath);
            sndFile = other.sndFile;  other.sndFile = nullptr;
            sfInfo = other.sfInfo;
            bufferA = std::move(other.bufferA);
            bufferB = std::move(other.bufferB);
            stateA.store(other.stateA.load());
            stateB.store(other.stateB.load());
            chunkStartA.store(other.chunkStartA.load());
            chunkStartB.store(other.chunkStartB.load());
            validFramesA.store(other.validFramesA.load());
            validFramesB.store(other.validFramesB.load());
            activeBuffer.store(other.activeBuffer.load());
            totalFrames = other.totalFrames;
            sampleRate = other.sampleRate;
            isLFE = other.isLFE;
            chunkFrames = other.chunkFrames;
        }
        return *this;
    }
};


// ─────────────────────────────────────────────────────────────────────────────
// Streaming — Manages all source streams and the background loader
// ─────────────────────────────────────────────────────────────────────────────

class Streaming {
public:

    Streaming(RealtimeConfig& config, EngineState& state)
        : mConfig(config), mState(state) {}

    ~Streaming() { shutdown(); }

    // ── Load all sources from a LUSID scene ──────────────────────────────
    // Opens each source WAV file and pre-loads the first chunk.
    // Must be called BEFORE starting the audio stream.
    //
    // The source name → filename convention follows WavUtils::loadSources():
    //   source key "1.1" → file "1.1.wav"
    //   source key "LFE" → file "LFE.wav"

    bool loadScene(const SpatialData& scene) {
        std::cout << "[Streaming] Loading " << scene.sources.size()
                  << " sources from: " << mConfig.sourcesFolder << std::endl;

        for (const auto& [sourceName, keyframes] : scene.sources) {
            // Build file path: sourcesFolder/sourceName.wav
            fs::path wavPath = fs::path(mConfig.sourcesFolder) / (sourceName + ".wav");

            if (!fs::exists(wavPath)) {
                std::cerr << "[Streaming] WARNING: Missing source WAV: "
                          << wavPath << " — skipping." << std::endl;
                continue;
            }

            // Create stream for this source
            auto stream = std::make_unique<SourceStream>();
            if (!stream->open(wavPath.string(), sourceName,
                              kDefaultChunkFrames, mConfig.sampleRate)) {
                std::cerr << "[Streaming] WARNING: Failed to open " 
                          << sourceName << " — skipping." << std::endl;
                continue;
            }

            // Load first chunk synchronously
            if (!stream->loadFirstChunk()) {
                std::cerr << "[Streaming] WARNING: Failed to preload "
                          << sourceName << " — skipping." << std::endl;
                continue;
            }

            std::cout << "  ✓ " << sourceName << " — "
                      << stream->totalFrames << " frames ("
                      << (double)stream->totalFrames / stream->sampleRate
                      << "s)" << (stream->isLFE ? " [LFE]" : "") << std::endl;

            mStreams[sourceName] = std::move(stream);
        }

        mState.numSources.store(static_cast<int>(mStreams.size()),
                                std::memory_order_relaxed);

        std::cout << "[Streaming] Loaded " << mStreams.size() << " sources."
                  << std::endl;

        return !mStreams.empty();
    }

    // ── Load all sources from a multichannel ADM WAV (direct streaming) ──
    // Instead of individual mono files, reads from one multichannel file
    // and de-interleaves channels into per-source buffers.
    //
    // Channel mapping convention (matches LUSID source key naming):
    //   "N.1" → ADM channel N → 0-based index (N-1)
    //   "LFE" → ADM channel 4 → 0-based index 3
    //
    // Only maps channels that appear in the scene's source list.

    bool loadSceneFromADM(const SpatialData& scene, const std::string& admFilePath) {
        std::cout << "[Streaming] Loading " << scene.sources.size()
                  << " sources from multichannel ADM: " << admFilePath << std::endl;

        mMultichannelMode = true;

        // Create the multichannel reader and open the ADM file
        mMultichannelReader = std::make_unique<MultichannelReader>();
        if (!mMultichannelReader->open(admFilePath, mConfig.sampleRate,
                                        kDefaultChunkFrames)) {
            std::cerr << "[Streaming] FATAL: Failed to open ADM file." << std::endl;
            return false;
        }

        uint64_t admTotalFrames = mMultichannelReader->totalFrames();
        int      admNumChannels = mMultichannelReader->numChannels();

        // Create buffer-only SourceStreams and map channels
        for (const auto& [sourceName, keyframes] : scene.sources) {
            // Parse source name to get 0-based channel index
            int channelIndex = parseChannelIndex(sourceName, admNumChannels);
            if (channelIndex < 0) {
                std::cerr << "[Streaming] WARNING: Cannot map source \""
                          << sourceName << "\" to ADM channel — skipping."
                          << std::endl;
                continue;
            }

            // Create a buffer-only stream (no individual file handle)
            auto stream = std::make_unique<SourceStream>();
            stream->initBuffersOnly(sourceName, kDefaultChunkFrames,
                                    mConfig.sampleRate, admTotalFrames);

            // Register with the multichannel reader
            mMultichannelReader->mapChannel(channelIndex, stream.get());

            std::cout << "  ✓ " << sourceName << " → ADM ch " << (channelIndex + 1)
                      << " (0-based: " << channelIndex << ")"
                      << (stream->isLFE ? " [LFE]" : "") << std::endl;

            mStreams[sourceName] = std::move(stream);
        }

        if (mStreams.empty()) {
            std::cerr << "[Streaming] FATAL: No sources could be mapped." << std::endl;
            return false;
        }

        // Read the first chunk from the multichannel file into buffer A
        // of all mapped streams (synchronous, before playback starts).
        if (!mMultichannelReader->readFirstChunk()) {
            std::cerr << "[Streaming] FATAL: Failed to read first chunk from ADM."
                      << std::endl;
            return false;
        }

        // Activate buffer A for playback on all streams
        for (auto& [name, stream] : mStreams) {
            stream->activeBuffer.store(0, std::memory_order_release);
            stream->stateA.store(StreamBufferState::PLAYING, std::memory_order_release);
        }

        mState.numSources.store(static_cast<int>(mStreams.size()),
                                std::memory_order_relaxed);

        std::cout << "[Streaming] Loaded " << mStreams.size() << " sources from ADM ("
                  << mMultichannelReader->numMappedChannels() << " of "
                  << admNumChannels << " channels mapped)." << std::endl;

        return true;
    }

    // ── Start the background loader thread ───────────────────────────────
    // Must be called AFTER loadScene() and BEFORE starting audio.

    void startLoader() {
        mLoaderRunning.store(true, std::memory_order_release);
        mLoaderThread = std::thread([this]() { loaderWorker(); });
        std::cout << "[Streaming] Background loader thread started." << std::endl;
    }

    // ── Get a sample for a given source at a global frame position ───────
    // Called from the audio callback — MUST be lock-free and real-time safe.

    float getSample(const std::string& sourceName, uint64_t globalFrame) const {
        auto it = mStreams.find(sourceName);
        if (it == mStreams.end()) return 0.0f;
        return it->second->getSample(globalFrame);
    }

    // ── Get a block of samples for a source into a pre-allocated buffer ──
    // More efficient than per-sample getSample() — copies a contiguous
    // block from the active buffer when possible.
    // Called from the audio callback — MUST be lock-free.

    void getBlock(const std::string& sourceName, uint64_t startFrame,
                  unsigned int numFrames, float* outBuffer) const {
        auto it = mStreams.find(sourceName);
        if (it == mStreams.end()) {
            std::memset(outBuffer, 0, numFrames * sizeof(float));
            return;
        }

        const SourceStream& src = *it->second;
        int active = src.activeBuffer.load(std::memory_order_acquire);

        if (active < 0) {
            std::memset(outBuffer, 0, numFrames * sizeof(float));
            return;
        }

        // Try to get the whole block from the active buffer
        const auto& buffer   = (active == 0) ? src.bufferA : src.bufferB;
        uint64_t bufStart    = (active == 0)
            ? src.chunkStartA.load(std::memory_order_acquire)
            : src.chunkStartB.load(std::memory_order_acquire);
        uint64_t bufValid    = (active == 0)
            ? src.validFramesA.load(std::memory_order_acquire)
            : src.validFramesB.load(std::memory_order_acquire);

        uint64_t endFrame = startFrame + numFrames;

        // Happy path: entire block fits in the active buffer
        if (startFrame >= bufStart && endFrame <= bufStart + bufValid) {
            std::memcpy(outBuffer, buffer.data() + (startFrame - bufStart),
                        numFrames * sizeof(float));
            return;
        }

        // Slow path: block spans a buffer boundary, or active buffer doesn't
        // have our data. Fall back to per-sample access (handles buffer switch).
        for (unsigned int i = 0; i < numFrames; ++i) {
            outBuffer[i] = src.getSample(startFrame + i);
        }
    }

    // ── Source queries ────────────────────────────────────────────────────

    /// Get the list of loaded source names.
    std::vector<std::string> sourceNames() const {
        std::vector<std::string> names;
        names.reserve(mStreams.size());
        for (const auto& [name, _] : mStreams) {
            names.push_back(name);
        }
        return names;
    }

    /// Check if a source is the LFE channel.
    bool isLFE(const std::string& sourceName) const {
        auto it = mStreams.find(sourceName);
        return (it != mStreams.end()) ? it->second->isLFE : false;
    }

    /// Get total frames for a source.
    uint64_t totalFrames(const std::string& sourceName) const {
        auto it = mStreams.find(sourceName);
        return (it != mStreams.end()) ? it->second->totalFrames : 0;
    }

    /// Number of loaded sources.
    size_t numSources() const { return mStreams.size(); }

    // ── Shutdown ─────────────────────────────────────────────────────────

    void shutdown() {
        // Stop loader thread
        mLoaderRunning.store(false, std::memory_order_release);
        if (mLoaderThread.joinable()) {
            mLoaderThread.join();
        }
        // Close multichannel reader if active
        if (mMultichannelReader) {
            mMultichannelReader->close();
            mMultichannelReader.reset();
        }
        mMultichannelMode = false;
        // Close all file handles
        for (auto& [name, stream] : mStreams) {
            stream->close();
        }
        mStreams.clear();
        std::cout << "[Streaming] Shutdown complete." << std::endl;
    }


private:

    // ── Background loader thread ─────────────────────────────────────────
    // Runs continuously, checking each source to see if the inactive buffer
    // needs to be filled with the next chunk. Sleeps briefly between scans
    // to avoid burning CPU.

    void loaderWorker() {
        while (mLoaderRunning.load(std::memory_order_acquire)) {

            // Get current playback position from engine state
            uint64_t currentFrame = mState.frameCounter.load(std::memory_order_relaxed);

            if (mMultichannelMode && mMultichannelReader) {
                // ── Multichannel (ADM direct) mode ───────────────────────
                // All streams share the same file and chunk boundaries.
                // We check ANY stream's active buffer to decide when to
                // trigger a bulk read + distribute for all channels at once.
                loaderWorkerMultichannel(currentFrame);
            } else {
                // ── Mono file mode (original behavior) ───────────────────
                loaderWorkerMono(currentFrame);
            }

            // Sleep to avoid busy-waiting. 2ms is well under the audio buffer
            // period (~10ms at 512 frames/48kHz) but frequent enough to catch
            // preload triggers in time.
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    /// Mono mode loader: each source has its own file, load independently.
    void loaderWorkerMono(uint64_t currentFrame) {
        for (auto& [name, stream] : mStreams) {
            if (!stream->sndFile) continue;

            int active = stream->activeBuffer.load(std::memory_order_acquire);
            if (active < 0) continue;

            // Determine which buffer is active and which is inactive
            uint64_t activeStart = (active == 0)
                ? stream->chunkStartA.load(std::memory_order_acquire)
                : stream->chunkStartB.load(std::memory_order_acquire);
            uint64_t activeValid = (active == 0)
                ? stream->validFramesA.load(std::memory_order_acquire)
                : stream->validFramesB.load(std::memory_order_acquire);

            int inactive = 1 - active;
            auto inactiveState = (inactive == 0)
                ? stream->stateA.load(std::memory_order_acquire)
                : stream->stateB.load(std::memory_order_acquire);

            // Check if we've consumed enough of the active buffer to
            // warrant preloading the next chunk into the inactive buffer.
            // Trigger at kPreloadThreshold (50%) of the active chunk.
            if (activeValid > 0 && inactiveState == StreamBufferState::EMPTY) {
                uint64_t threshold = activeStart +
                    static_cast<uint64_t>(activeValid * kPreloadThreshold);

                if (currentFrame >= threshold) {
                    // Calculate the start of the next chunk
                    uint64_t nextChunkStart = activeStart + stream->chunkFrames;

                    // Don't load past the end of the file
                    if (nextChunkStart < stream->totalFrames) {
                        stream->loadChunkInto(inactive, nextChunkStart);
                    }
                }
            }
        }
    }

    /// Multichannel mode loader: one shared file, bulk read + de-interleave.
    /// All streams share chunk boundaries — check one representative stream
    /// to decide when to trigger the next bulk read.
    void loaderWorkerMultichannel(uint64_t currentFrame) {
        // Find a representative stream (first one) to check timing
        if (mStreams.empty()) return;
        auto& representative = mStreams.begin()->second;

        int active = representative->activeBuffer.load(std::memory_order_acquire);
        if (active < 0) return;

        uint64_t activeStart = (active == 0)
            ? representative->chunkStartA.load(std::memory_order_acquire)
            : representative->chunkStartB.load(std::memory_order_acquire);
        uint64_t activeValid = (active == 0)
            ? representative->validFramesA.load(std::memory_order_acquire)
            : representative->validFramesB.load(std::memory_order_acquire);

        int inactive = 1 - active;
        auto inactiveState = (inactive == 0)
            ? representative->stateA.load(std::memory_order_acquire)
            : representative->stateB.load(std::memory_order_acquire);

        // Same preload logic as mono mode, but applied to all channels at once
        if (activeValid > 0 && inactiveState == StreamBufferState::EMPTY) {
            uint64_t threshold = activeStart +
                static_cast<uint64_t>(activeValid * kPreloadThreshold);

            if (currentFrame >= threshold) {
                uint64_t nextChunkStart = activeStart + representative->chunkFrames;

                if (nextChunkStart < mMultichannelReader->totalFrames()) {
                    // One bulk read + de-interleave fills ALL mapped streams
                    mMultichannelReader->readAndDistribute(nextChunkStart, inactive);
                }
            }
        }
    }

    // ── Channel index parsing ────────────────────────────────────────────
    // Maps LUSID source key names to 0-based ADM channel indices.
    //
    // Convention:
    //   "N.1"  → ADM track N → 0-based index (N - 1)
    //     e.g., "1.1" → 0, "11.1" → 10, "24.1" → 23
    //   "LFE"  → ADM channel 4 → 0-based index 3
    //     (standard ADM bed layout: L=1, R=2, C=3, LFE=4, ...)

    static int parseChannelIndex(const std::string& sourceName, int numChannels) {
        // Handle LFE special case
        if (sourceName == "LFE") {
            return (numChannels >= 4) ? 3 : -1;
        }

        // Parse "N.1" pattern: extract the integer N before the dot
        size_t dotPos = sourceName.find('.');
        if (dotPos == std::string::npos || dotPos == 0) {
            return -1;  // No dot found or starts with dot
        }

        try {
            int trackNum = std::stoi(sourceName.substr(0, dotPos));
            int index = trackNum - 1;  // 1-based → 0-based
            if (index >= 0 && index < numChannels) {
                return index;
            }
        } catch (...) {
            // Not a valid integer prefix
        }

        return -1;
    }

    // ── Member data ──────────────────────────────────────────────────────

    RealtimeConfig& mConfig;
    EngineState&    mState;

    // All active source streams, keyed by source name (e.g., "1.1", "LFE")
    std::map<std::string, std::unique_ptr<SourceStream>> mStreams;

    // ── Multichannel (ADM direct) mode ───────────────────────────────────
    // When true, sources are read from one multichannel file via the reader,
    // not from individual mono files.
    bool mMultichannelMode = false;
    std::unique_ptr<MultichannelReader> mMultichannelReader;

    // Background loader thread
    std::thread          mLoaderThread;
    std::atomic<bool>    mLoaderRunning{false};
};


// ─────────────────────────────────────────────────────────────────────────────
// MultichannelReader method implementations
// ─────────────────────────────────────────────────────────────────────────────
// These are defined here (after SourceStream is fully defined) rather than in
// MultichannelReader.hpp, because they need access to SourceStream's members.
// This is standard C++ practice for breaking circular header dependencies.

inline void MultichannelReader::deinterleaveInto(
    SourceStream* stream, int bufIdx, int channelIndex,
    uint64_t framesRead, uint64_t fileFrame)
{
    auto& buffer = (bufIdx == 0) ? stream->bufferA : stream->bufferB;
    auto& state  = (bufIdx == 0) ? stream->stateA  : stream->stateB;
    auto& start  = (bufIdx == 0) ? stream->chunkStartA : stream->chunkStartB;
    auto& valid  = (bufIdx == 0) ? stream->validFramesA : stream->validFramesB;

    state.store(StreamBufferState::LOADING, std::memory_order_release);

    // De-interleave: extract this channel from the interleaved buffer.
    // Interleaved layout: [ch0_f0, ch1_f0, ..., chN_f0, ch0_f1, ch1_f1, ...]
    // For frame i, channel c: interleavedBuffer[i * numChannels + c]
    const float* src = mInterleavedBuffer.data();
    float* dst = buffer.data();

    for (uint64_t i = 0; i < framesRead; ++i) {
        dst[i] = src[i * mNumChannels + channelIndex];
    }

    // Zero-fill remainder if we read less than the chunk size
    if (framesRead < stream->chunkFrames) {
        std::memset(dst + framesRead, 0,
                    (stream->chunkFrames - framesRead) * sizeof(float));
    }

    start.store(fileFrame, std::memory_order_release);
    valid.store(framesRead, std::memory_order_release);
    state.store(StreamBufferState::READY, std::memory_order_release);
}

inline void MultichannelReader::zeroFillBuffer(
    SourceStream* stream, int bufIdx, uint64_t fileFrame)
{
    auto& buffer = (bufIdx == 0) ? stream->bufferA : stream->bufferB;
    auto& state  = (bufIdx == 0) ? stream->stateA  : stream->stateB;
    auto& start  = (bufIdx == 0) ? stream->chunkStartA : stream->chunkStartB;
    auto& valid  = (bufIdx == 0) ? stream->validFramesA : stream->validFramesB;

    state.store(StreamBufferState::LOADING, std::memory_order_release);
    std::memset(buffer.data(), 0, stream->chunkFrames * sizeof(float));
    start.store(fileFrame, std::memory_order_release);
    valid.store(0, std::memory_order_release);
    state.store(StreamBufferState::READY, std::memory_order_release);
}
