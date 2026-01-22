// VBAPRenderer - spatial audio renderer using AlloLib's VBAP implementation
//
// important notes if you need to debug this again:
// 
// 1. al::Speaker constructor expects angles in DEGREES not radians
//    the layout JSON has radians so we convert in the constructor
//    without this VBAP silently produces zeros
//
// 2. AlloSphere hardware uses non-consecutive channel numbers 1-60 with gaps
//    but we use consecutive 0-53 indices for VBAP and the output WAV
//    this avoids out-of-bounds crashes when accessing AudioIOData buffers
//    can remap to hardware channels later if needed
//
// 3. AudioIOData initialization order matters
//    must call framesPerBuffer before channelsOut or you get assertion failures
//
// 4. VBAP uses += to accumulate sources so call zeroOut before each block
//
// 5. must call audioIO.frame(0) before reading output samples
//
// 6. interpolateDir() must handle edge cases: empty keyframes, t outside range,
//    degenerate directions - see implementation for NaN-safe version

#pragma once

#include <map>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <al/math/al_Vec.hpp>
#include <al/sound/al_Vbap.hpp>
#include <al/io/al_AudioIOData.hpp>

#include "JSONLoader.hpp"
#include "LayoutLoader.hpp"
#include "WavUtils.hpp"

// Render configuration options
struct RenderConfig {
    float masterGain = 0.25f;       // Output gain to prevent clipping (0.0-1.0)
    std::string soloSource = "";    // If non-empty, only render this source
    double t0 = -1.0;               // Start time in seconds (-1 = from beginning)
    double t1 = -1.0;               // End time in seconds (-1 = to end)
    bool debugDiagnostics = false;  // Enable per-block diagnostics logging
    std::string debugOutputDir = "processedData/debug";  // Where to write debug files
};

// Render statistics for diagnostics
struct RenderStats {
    std::vector<float> channelRMS;      // RMS level per channel in dBFS
    std::vector<float> channelPeak;     // Peak absolute value per channel
    std::vector<int> channelNaNCount;   // NaN count per channel
    std::vector<int> channelInfCount;   // Inf count per channel
    int totalSamples = 0;
    int numChannels = 0;
    int numSources = 0;
    double durationSec = 0.0;
    
    // Per-source fallback statistics
    std::unordered_map<std::string, int> sourceFallbackCount;
    int totalFallbackBlocks = 0;
};

class VBAPRenderer {
public:
    VBAPRenderer(const SpeakerLayoutData &layout,
                 const SpatialData &spatial,
                 const std::map<std::string, MonoWavData> &sources);

    // Main render with default config
    MultiWavData render();
    
    // Render with custom configuration
    MultiWavData render(const RenderConfig &config);
    
    // Get statistics from last render (call after render())
    RenderStats getLastRenderStats() const { return mLastStats; }

private:
    SpeakerLayoutData mLayout;
    SpatialData mSpatial;
    const std::map<std::string, MonoWavData> &mSources;
    
    al::Speakers mSpeakers;
    al::Vbap mVBAP;
    
    // not currently used but left here in case you need to remap channels later
    // would map consecutive VBAP indices to AlloSphere hardware channels
    std::vector<int> mVbapToDevice;

    float blockSize = 256.0f;
    
    // Statistics from last render
    RenderStats mLastStats;
    
    // Per-source direction tracking for safe fallback
    // These are reset at start of each render
    std::unordered_map<std::string, al::Vec3f> mLastGoodDir;
    std::unordered_set<std::string> mWarnedDegenerate;
    std::unordered_map<std::string, int> mFallbackCount;

    // Helper: check if all components are finite
    static bool finite3(const al::Vec3f& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    
    // Helper: compute unit direction from Cartesian (safe, validates output)
    static al::Vec3f safeNormalize(const al::Vec3f& v);
    
    // Get safe direction for a source, using last-good or fallback if invalid
    // This is the main entry point for direction computation in the render loop
    al::Vec3f safeDirForSource(const std::string& name, const std::vector<Keyframe>& kfs, double t);

    // linear interpolation between spatial keyframes (raw, may return invalid)
    al::Vec3f interpolateDirRaw(const std::vector<Keyframe> &kfs, double t);
    
    // Compute statistics on rendered output
    void computeRenderStats(const MultiWavData &output);
    
    // Detect and fix keyframe time units (samples vs seconds)
    void normalizeKeyframeTimes(double durationSec, size_t totalSamples, int sr);
    
    // Reset per-render state (call at start of render)
    void resetPerRenderState();
    
    // Print end-of-render fallback summary
    void printFallbackSummary(int totalBlocks);
};
