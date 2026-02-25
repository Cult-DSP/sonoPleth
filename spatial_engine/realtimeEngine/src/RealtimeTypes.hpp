// RealtimeTypes.hpp — Shared data types for the real-time spatial audio engine
//
// These structs are used across multiple agents (Backend, Streaming, Pose,
// Spatializer, etc.) to pass data through the processing pipeline.
//
// DESIGN NOTES:
// - Structs here must be POD-friendly or at least trivially copyable where
//   they are shared between threads (audio callback vs control thread).
// - The audio callback thread must NEVER allocate, lock, or do I/O.
//   Any struct read by the audio thread should be accessed via atomic pointers,
//   double-buffering, or lock-free queues — that coordination is handled by
//   the agents themselves, not by these types.
// - Types are intentionally kept simple for Phase 1. Future phases will add
//   fields (e.g., per-source gain, mute flags, velocity for Doppler, etc.).
//
// PROVENANCE:
// - SpeakerLayoutData, SpeakerData, subwooferData → reused directly from
//   spatial_engine/src/LayoutLoader.hpp (shared via include path)
// - SpatialData, Keyframe, TimeUnit → reused directly from
//   spatial_engine/src/JSONLoader.hpp (shared via include path)
// - MonoWavData, MultiWavData → reused directly from
//   spatial_engine/src/WavUtils.hpp (shared via include path)
//
// This file defines ONLY the additional types needed for real-time operation
// that don't exist in the offline renderer's headers.

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// ElevationMode — Elevation handling for directions outside speaker coverage
// ─────────────────────────────────────────────────────────────────────────────
// Replicated from SpatialRenderer.hpp so the real-time engine doesn't depend
// on the offline renderer headers. Must stay in sync.
enum class ElevationMode {
    Clamp,              // Hard clip elevation to layout bounds
    RescaleAtmosUp,     // Default. Assumes content in [0, +π/2]. Maps to layout range.
    RescaleFullSphere   // Assumes content in [-π/2, +π/2]. Maps to layout range.
};

// ─────────────────────────────────────────────────────────────────────────────
// RealtimeConfig — Global configuration for the real-time engine
// ─────────────────────────────────────────────────────────────────────────────
// Set once at startup, read-only during playback.
// The audio thread may read any field without synchronization because these
// don't change after init (except masterGain which is std::atomic).

struct RealtimeConfig {
    // ── Audio device settings ────────────────────────────────────────────
    int    sampleRate       = 48000;   // Audio sample rate in Hz
    int    bufferSize       = 512;     // Frames per audio callback buffer
    int    inputChannels    = 0;       // Input channels (0 = output only)

    // ── Layout-derived channel count ─────────────────────────────────────
    // COMPUTED from the speaker layout at load time — never set by the user.
    // Formula (matches offline SpatialRenderer.cpp):
    //   maxChannel = max(numSpeakers - 1, max(subwooferDeviceChannels))
    //   outputChannels = maxChannel + 1
    // This means the output buffer may contain gap channels (e.g., Allosphere
    // channels 13-16, 47-48 are unused). A future Channel Remap agent will
    // map these logical render channels to physical device outputs.
    // For now, AudioIO opens with this many channels (identity mapping).
    int    outputChannels   = 0;       // Set by Spatializer::init() from layout

    // ── Spatializer settings (mirrors offline RenderConfig) ──────────────
    float  dbapFocus        = 1.0f;    // DBAP focus/rolloff exponent (0.2–5.0)
    ElevationMode elevationMode = ElevationMode::RescaleAtmosUp;  // Elevation mapping

    // ── Gain settings ────────────────────────────────────────────────────
    // masterGain is atomic because the GUI/control thread may adjust it
    // while the audio thread is reading it. Phase 6 (Compensation and Gain
    // Agent) adds three more atomics on the same pattern:
    std::atomic<float> masterGain{0.5f};          // Global output gain (0.0–1.0)
    std::atomic<float> loudspeakerMix{1.0f};      // post-DBAP main-channel trim (±10 dB)
    std::atomic<float> subMix{1.0f};              // post-DBAP sub-channel trim  (±10 dB)
    std::atomic<bool>  focusAutoCompensation{false}; // auto-update loudspeakerMix on focus change

    // ── File paths (set at startup, read-only after) ─────────────────────
    std::string layoutPath;       // Speaker layout JSON
    std::string scenePath;        // LUSID scene JSON (positions/trajectories)
    std::string sourcesFolder;    // Folder containing mono source WAV files
    std::string admFile;          // Multichannel ADM WAV file (direct streaming)
                                  // If non-empty, use ADM direct mode instead of
                                  // mono sources folder. Mutually exclusive with
                                  // sourcesFolder.

    // ── Playback control ─────────────────────────────────────────────────
    std::atomic<bool> playing{false};   // True when audio should be output
    std::atomic<bool> shouldExit{false}; // True when engine should shut down
};


// ─────────────────────────────────────────────────────────────────────────────
// EngineState — Runtime state visible to all agents (read-mostly)
// ─────────────────────────────────────────────────────────────────────────────
// This struct is updated by the audio thread and read by the GUI/control
// thread for monitoring. All fields are atomic for safe cross-thread reads.
// This is intentionally minimal for Phase 1 — future phases add per-source
// and per-channel metrics.

struct EngineState {
    // ── Playback position ────────────────────────────────────────────────
    std::atomic<uint64_t> frameCounter{0};  // Current playback position in samples
    std::atomic<double>   playbackTimeSec{0.0}; // Current playback time in seconds

    // ── Performance monitoring ───────────────────────────────────────────
    std::atomic<float>    cpuLoad{0.0f};    // Audio thread CPU usage (0.0–1.0)
    std::atomic<uint64_t> xrunCount{0};     // Buffer underrun count

    // ── Scene info (set once at load time) ───────────────────────────────
    std::atomic<int>      numSources{0};    // Number of active audio sources
    std::atomic<int>      numSpeakers{0};   // Number of speakers in layout
    std::atomic<double>   sceneDuration{0.0}; // Total scene duration in seconds
};

