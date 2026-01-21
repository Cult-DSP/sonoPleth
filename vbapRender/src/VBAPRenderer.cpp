#include "VBAPRenderer.hpp"
#include <cmath>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <numeric>
#include <filesystem>

namespace fs = std::filesystem;

VBAPRenderer::VBAPRenderer(const SpeakerLayoutData &layout,
                           const SpatialData &spatial,
                           const std::map<std::string, MonoWavData> &sources)
    : mLayout(layout), mSpatial(spatial), mSources(sources),
      mSpeakers(), mVBAP(mSpeakers, true)

      //note: might need to remap channels for sphere later? to account for gaps in channel numbering?
{
    // CRITICAL FIX 1: AlloLib's al::Speaker expects angles in DEGREES not radians
    // The AlloSphere layout JSON stores angles in radians but al::Speaker internally
    // converts to radians using toRad() which assumes degree input
    // Without this conversion you get speaker positions at completely wrong angles
    // like -77.7 radians instead of -77.7 degrees which is way outside valid range
    // This caused VBAP to fail silently and produce zero output
    //
    // CRITICAL FIX 2: AlloSphere hardware uses non-consecutive channel numbers 1-60 with gaps
    // but VBAP needs consecutive 0-based indices for AudioIOData buffer access
    // We use array index i as the VBAP channel and ignore the original deviceChannel numbers
    // The output WAV will have consecutive channels 0-53 which can be remapped later
    // if you need the original hardware channel routing
    // Old approach tried to preserve deviceChannel which caused out-of-bounds crashes
    // because AudioIOData only allocates channels 0 to numSpeakers-1
    
    for (size_t i = 0; i < layout.speakers.size(); i++) {
        const auto &spk = layout.speakers[i];
        mSpeakers.emplace_back(al::Speaker(
            i,                                    // consecutive 0-based channel index
            spk.azimuth * 180.0f / M_PI,          // radians to degrees
            spk.elevation * 180.0f / M_PI,        // radians to degrees
            0,                                    // group id
            spk.radius                            // distance from center
        ));
    }
    
    // compile builds the speaker triplet mesh for VBAP algorithm
    // this finds all valid triangles of 3 speakers that can spatialize sound
    mVBAP = al::Vbap(mSpeakers, true);
    mVBAP.compile();
}

al::Vec3f VBAPRenderer::interpolateDir(const std::vector<Keyframe> &kfs, double t) {
    // Linear interpolation between keyframes for smooth spatial motion.
    // Takes time in seconds and returns normalized direction vector.
    //
    // FIXED: Previous version left k1/k2 uninitialized if t was outside keyframe range,
    // causing NaNs and unpredictable VBAP output (silence/cutouts/chaos).
    // Now properly clamps to first/last keyframe and handles edge cases.
    
    // Safe fallback if no keyframes
    if (kfs.empty()) {
        std::cerr << "Warning: interpolateDir called with empty keyframes, using fallback direction\n";
        return al::Vec3f(0.0f, 1.0f, 0.0f);  // safe default: front
    }
    
    // Single keyframe - just return its direction
    if (kfs.size() == 1) {
        al::Vec3f v(kfs[0].x, kfs[0].y, kfs[0].z);
        float mag = v.mag();
        if (mag < 1e-6f || !std::isfinite(mag)) {
            std::cerr << "Warning: degenerate single keyframe direction, using fallback\n";
            return al::Vec3f(0.0f, 1.0f, 0.0f);
        }
        return v.normalize();
    }
    
    // Clamp to first keyframe if before all keyframes
    if (t <= kfs.front().time) {
        al::Vec3f v(kfs.front().x, kfs.front().y, kfs.front().z);
        float mag = v.mag();
        if (mag < 1e-6f || !std::isfinite(mag)) {
            return al::Vec3f(0.0f, 1.0f, 0.0f);
        }
        return v.normalize();
    }
    
    // Clamp to last keyframe if after all keyframes
    if (t >= kfs.back().time) {
        al::Vec3f v(kfs.back().x, kfs.back().y, kfs.back().z);
        float mag = v.mag();
        if (mag < 1e-6f || !std::isfinite(mag)) {
            return al::Vec3f(0.0f, 1.0f, 0.0f);
        }
        return v.normalize();
    }
    
    // Find the keyframe segment containing time t
    const Keyframe *k1 = &kfs[0];
    const Keyframe *k2 = &kfs[1];
    for (size_t i = 0; i < kfs.size() - 1; i++) {
        if (t >= kfs[i].time && t <= kfs[i+1].time) {
            k1 = &kfs[i];
            k2 = &kfs[i+1];
            break;
        }
    }
    
    // Handle degenerate time segments (dt <= 0)
    double dt = k2->time - k1->time;
    if (dt <= 1e-9) {
        al::Vec3f v(k1->x, k1->y, k1->z);
        float mag = v.mag();
        if (mag < 1e-6f || !std::isfinite(mag)) {
            return al::Vec3f(0.0f, 1.0f, 0.0f);
        }
        return v.normalize();
    }
    
    // Linear interpolation
    double u = (t - k1->time) / dt;
    u = std::max(0.0, std::min(1.0, u));  // clamp u to [0,1] for safety
    
    al::Vec3f v(
        (1.0 - u) * k1->x + u * k2->x,
        (1.0 - u) * k1->y + u * k2->y,
        (1.0 - u) * k1->z + u * k2->z
    );
    
    // Validate and normalize
    float mag = v.mag();
    if (mag < 1e-6f || !std::isfinite(mag)) {
        std::cerr << "Warning: interpolation produced degenerate direction at t=" << t << "\n";
        return al::Vec3f(0.0f, 1.0f, 0.0f);
    }
    
    return v.normalize();
}

// Detect if keyframe times are in samples instead of seconds and convert
void VBAPRenderer::normalizeKeyframeTimes(double durationSec, size_t totalSamples, int sr) {
    for (auto &[name, kfs] : mSpatial.sources) {
        if (kfs.empty()) continue;
        
        double maxTime = 0.0;
        for (const auto &kf : kfs) {
            maxTime = std::max(maxTime, kf.time);
        }
        
        // Heuristic: if maxTime > durationSec * 10 and maxTime <= totalSamples * 1.1,
        // times are likely in samples, not seconds
        if (maxTime > durationSec * 10.0 && maxTime <= (double)totalSamples * 1.1) {
            std::cout << "  [Time Unit Fix] Source '" << name << "': converting times from samples to seconds\n";
            std::cout << "    maxTime=" << maxTime << " vs durationSec=" << durationSec << "\n";
            for (auto &kf : kfs) {
                kf.time /= (double)sr;
            }
        }
    }
}

// Compute statistics on rendered output
void VBAPRenderer::computeRenderStats(const MultiWavData &output) {
    mLastStats = RenderStats();
    mLastStats.numChannels = output.channels;
    mLastStats.totalSamples = output.samples.empty() ? 0 : output.samples[0].size();
    mLastStats.durationSec = (double)mLastStats.totalSamples / output.sampleRate;
    mLastStats.numSources = mSpatial.sources.size();
    
    mLastStats.channelRMS.resize(output.channels, 0.0f);
    mLastStats.channelPeak.resize(output.channels, 0.0f);
    mLastStats.channelNaNCount.resize(output.channels, 0);
    mLastStats.channelInfCount.resize(output.channels, 0);
    
    for (int ch = 0; ch < output.channels; ch++) {
        const auto &samples = output.samples[ch];
        double sumSq = 0.0;
        float peak = 0.0f;
        int nanCount = 0, infCount = 0;
        
        for (float s : samples) {
            if (std::isnan(s)) {
                nanCount++;
            } else if (std::isinf(s)) {
                infCount++;
            } else {
                sumSq += (double)s * s;
                peak = std::max(peak, std::abs(s));
            }
        }
        
        double rms = std::sqrt(sumSq / samples.size());
        // Convert to dBFS (0 dB = full scale = 1.0)
        float rmsDB = (rms > 1e-10) ? 20.0f * std::log10((float)rms) : -120.0f;
        
        mLastStats.channelRMS[ch] = rmsDB;
        mLastStats.channelPeak[ch] = peak;
        mLastStats.channelNaNCount[ch] = nanCount;
        mLastStats.channelInfCount[ch] = infCount;
    }
}

// Default render (uses default config)
MultiWavData VBAPRenderer::render() {
    RenderConfig defaultConfig;
    return render(defaultConfig);
}

// Main render function with configuration options
MultiWavData VBAPRenderer::render(const RenderConfig &config) {
    int sr = mSpatial.sampleRate;
    int numSpeakers = mLayout.speakers.size();

    size_t totalSamples = 0;
    for (auto &[name, wav] : mSources) {
        totalSamples = std::max(totalSamples, wav.samples.size());
    }
    
    double durationSec = (double)totalSamples / sr;
    
    // Detect and fix keyframe time units (samples vs seconds)
    normalizeKeyframeTimes(durationSec, totalSamples, sr);

    // Determine render range based on config
    size_t startSample = 0;
    size_t endSample = totalSamples;
    
    if (config.t0 >= 0.0) {
        startSample = (size_t)(config.t0 * sr);
        startSample = std::min(startSample, totalSamples);
    }
    if (config.t1 >= 0.0) {
        endSample = (size_t)(config.t1 * sr);
        endSample = std::min(endSample, totalSamples);
    }
    
    size_t renderSamples = (endSample > startSample) ? (endSample - startSample) : 0;

    std::cout << "Rendering " << renderSamples << " samples (" 
              << (double)renderSamples / sr << " sec) to " 
              << numSpeakers << " speakers from " << mSources.size() << " sources\n";
    std::cout << "  Master gain: " << config.masterGain << "\n";
    
    if (!config.soloSource.empty()) {
        std::cout << "  SOLO MODE: Only rendering source '" << config.soloSource << "'\n";
    }
    if (config.t0 >= 0.0 || config.t1 >= 0.0) {
        std::cout << "  TIME WINDOW: " << (config.t0 >= 0.0 ? config.t0 : 0.0) 
                  << "s to " << (config.t1 >= 0.0 ? config.t1 : durationSec) << "s\n";
    }

    // output uses consecutive channels 0 to numSpeakers-1
    // this is simpler than trying to maintain the AlloSphere hardware channel gaps
    // if you need to remap to hardware channels later just create a channel routing map
    MultiWavData out;
    out.sampleRate = sr;
    out.channels = numSpeakers;
    out.samples.resize(numSpeakers);
    for (auto &c : out.samples) c.resize(renderSamples, 0.0f);

    // CRITICAL: must call framesPerBuffer BEFORE channelsOut
    // otherwise AudioIOData throws assertion failures about buffer size not being set
    // the AlloLib API is picky about initialization order
    const int bufferSize = 512;
    al::AudioIOData audioIO;
    audioIO.framesPerBuffer(bufferSize);
    audioIO.framesPerSecond(sr);
    audioIO.channelsIn(0);
    audioIO.channelsOut(numSpeakers);
    
    std::vector<float> sourceBuffer(bufferSize, 0.0f);  // initialize to zero
    
    // Optional debug logging
    std::ofstream blockLog;
    if (config.debugDiagnostics) {
        fs::create_directories(config.debugOutputDir);
        blockLog.open(config.debugOutputDir + "/block_stats.log");
        blockLog << "block,time_sec,peak,nonfinite_count,active_speakers\n";
    }
    
    int blocksProcessed = 0;
    for (size_t blockStart = startSample; blockStart < endSample; blockStart += bufferSize) {
        size_t blockEnd = std::min(endSample, blockStart + bufferSize);
        size_t blockLen = blockEnd - blockStart;
        size_t outBlockStart = blockStart - startSample;  // offset into output buffer
        
        if (blocksProcessed % 1000 == 0) {
            std::cout << "  Block " << blocksProcessed << " (" 
                      << (int)(100.0 * (blockStart - startSample) / renderSamples) << "%)\n" << std::flush;
        }
        blocksProcessed++;
        
        // zero out the audio buffer before accumulating sources
        // VBAP uses += to accumulate multiple sources into the same speakers
        audioIO.zeroOut();
        
        int sourceIdx = 0;
        for (auto &[name, kfs] : mSpatial.sources) {
            // Solo mode: skip sources that aren't the solo target
            if (!config.soloSource.empty() && name != config.soloSource) {
                continue;
            }
            
            // Check if source exists in loaded sources
            auto srcIt = mSources.find(name);
            if (srcIt == mSources.end()) {
                continue;  // Source not found, skip
            }
            const MonoWavData &src = srcIt->second;
            
            // FIXED: Zero entire buffer first to prevent stale tail data
            // from entering VBAP if it reads beyond blockLen
            std::fill(sourceBuffer.begin(), sourceBuffer.end(), 0.0f);
            
            // copy source samples into buffer for this block
            for (size_t i = 0; i < blockLen; i++) {
                size_t globalIdx = blockStart + i;
                sourceBuffer[i] = (globalIdx < src.samples.size()) ? src.samples[globalIdx] : 0.0f;
            }
            
            // get spatial direction for this source at current time
            double timeSec = (double)blockStart / (double)sr;
            al::Vec3f dir = interpolateDir(kfs, timeSec);
            
            // FIXED: Reset frame cursor before each renderBuffer call
            // AudioIOData has internal frame state that may persist across calls
            audioIO.frame(0);
            
            // renderBuffer finds the best speaker triplet for this direction
            // calculates VBAP gains and mixes the source into the output channels
            // this accumulates into audioIO so multiple sources can overlap
            mVBAP.renderBuffer(audioIO, dir, sourceBuffer.data(), blockLen);
            sourceIdx++;
        }
        
        // copy the rendered audio from AudioIOData into our output buffer
        // must call frame(0) to reset the read position before accessing samples
        audioIO.frame(0);
        
        // Per-block diagnostics (rate-limited)
        float blockPeak = 0.0f;
        int nonfiniteCount = 0;
        int activeSpeakers = 0;
        
        for (size_t i = 0; i < blockLen; i++) {
            for (int ch = 0; ch < numSpeakers; ch++) {
                float sample = audioIO.out(ch, i);
                
                // Check for non-finite values
                if (!std::isfinite(sample)) {
                    nonfiniteCount++;
                    sample = 0.0f;  // Sanitize
                }
                
                // Apply master gain to prevent clipping
                sample *= config.masterGain;
                
                // Track peak for diagnostics
                blockPeak = std::max(blockPeak, std::abs(sample));
                
                // FIXED: Use assignment instead of accumulation
                // Each sample index is written once per block, += was causing potential double-add
                out.samples[ch][outBlockStart + i] = sample;
            }
        }
        
        // Count active speakers (RMS above threshold in this block)
        if (config.debugDiagnostics && blocksProcessed % 200 == 0) {
            for (int ch = 0; ch < numSpeakers; ch++) {
                float chPeak = 0.0f;
                for (size_t i = 0; i < blockLen; i++) {
                    chPeak = std::max(chPeak, std::abs(out.samples[ch][outBlockStart + i]));
                }
                if (chPeak > 1e-6f) activeSpeakers++;
            }
            
            double timeSec = (double)blockStart / (double)sr;
            blockLog << blocksProcessed << "," << timeSec << "," << blockPeak 
                     << "," << nonfiniteCount << "," << activeSpeakers << "\n";
        }
    }
    
    if (blockLog.is_open()) {
        blockLog.close();
    }
    
    // Compute and store render statistics
    computeRenderStats(out);
    
    // Log summary statistics
    std::cout << "\nRender Statistics:\n";
    int silentChannels = 0;
    int clippingChannels = 0;
    int nanChannels = 0;
    float overallPeak = 0.0f;
    
    for (int ch = 0; ch < numSpeakers; ch++) {
        if (mLastStats.channelRMS[ch] < -85.0f) silentChannels++;
        if (mLastStats.channelPeak[ch] > 1.0f) clippingChannels++;
        if (mLastStats.channelNaNCount[ch] > 0) nanChannels++;
        overallPeak = std::max(overallPeak, mLastStats.channelPeak[ch]);
    }
    
    std::cout << "  Overall peak: " << overallPeak << " (" 
              << 20.0f * std::log10(std::max(overallPeak, 1e-10f)) << " dBFS)\n";
    std::cout << "  Near-silent channels (< -85 dBFS): " << silentChannels << "/" << numSpeakers << "\n";
    std::cout << "  Clipping channels (peak > 1.0): " << clippingChannels << "\n";
    std::cout << "  Channels with NaN: " << nanChannels << "\n";
    
    // Write statistics to JSON if diagnostics enabled
    if (config.debugDiagnostics) {
        std::ofstream statsFile(config.debugOutputDir + "/render_stats.json");
        statsFile << "{\n";
        statsFile << "  \"totalSamples\": " << mLastStats.totalSamples << ",\n";
        statsFile << "  \"durationSec\": " << mLastStats.durationSec << ",\n";
        statsFile << "  \"numChannels\": " << mLastStats.numChannels << ",\n";
        statsFile << "  \"numSources\": " << mLastStats.numSources << ",\n";
        statsFile << "  \"overallPeak\": " << overallPeak << ",\n";
        statsFile << "  \"silentChannels\": " << silentChannels << ",\n";
        statsFile << "  \"clippingChannels\": " << clippingChannels << ",\n";
        statsFile << "  \"nanChannels\": " << nanChannels << ",\n";
        statsFile << "  \"masterGain\": " << config.masterGain << ",\n";
        statsFile << "  \"channelRMS\": [";
        for (int i = 0; i < numSpeakers; i++) {
            statsFile << mLastStats.channelRMS[i];
            if (i < numSpeakers - 1) statsFile << ", ";
        }
        statsFile << "],\n";
        statsFile << "  \"channelPeak\": [";
        for (int i = 0; i < numSpeakers; i++) {
            statsFile << mLastStats.channelPeak[i];
            if (i < numSpeakers - 1) statsFile << ", ";
        }
        statsFile << "]\n";
        statsFile << "}\n";
        statsFile.close();
        std::cout << "  Debug stats written to " << config.debugOutputDir << "/\n";
    }
    
    std::cout << "\n";
    return out;
}
