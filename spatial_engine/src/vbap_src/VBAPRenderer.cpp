#include "VBAPRenderer.hpp"
#include <cmath>
#include <iostream>
#include <iomanip>
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

// Reset per-render state (call at start of each render)
void VBAPRenderer::resetPerRenderState() {
    mLastGoodDir.clear();
    mWarnedDegenerate.clear();
    mFallbackCount.clear();
}

// Safe normalize: returns fallback if input is degenerate
al::Vec3f VBAPRenderer::safeNormalize(const al::Vec3f& v) {
    float mag = v.mag();
    if (mag < 1e-6f || !std::isfinite(mag)) {
        return al::Vec3f(0.0f, 1.0f, 0.0f);  // fallback: front
    }
    return v / mag;
}

// Spherical linear interpolation between two unit vectors
// Returns smooth interpolation on the unit sphere from a (t=0) to b (t=1)
al::Vec3f VBAPRenderer::slerpDir(const al::Vec3f& a, const al::Vec3f& b, float t) {
    // Clamp t to [0,1]
    t = std::max(0.0f, std::min(1.0f, t));
    
    // Compute cosine of angle between directions
    float dot = a.dot(b);
    
    // Clamp to handle numerical errors
    dot = std::max(-1.0f, std::min(1.0f, dot));
    
    // If vectors are very close, use linear interpolation to avoid division by zero
    if (dot > 0.9995f) {
        al::Vec3f result = a + t * (b - a);
        return safeNormalize(result);
    }
    
    // If vectors are nearly opposite, pick a perpendicular axis
    if (dot < -0.9995f) {
        // Find a perpendicular vector
        al::Vec3f perp = (std::abs(a.x) < 0.9f) ? al::Vec3f(1,0,0) : al::Vec3f(0,1,0);
        perp = (a.cross(perp)).normalize();
        // Rotate around perpendicular axis
        float theta = M_PI * t;
        return a * std::cos(theta) + perp * std::sin(theta);
    }
    
    // Standard SLERP formula
    float theta = std::acos(dot);
    float sinTheta = std::sin(theta);
    
    float wa = std::sin((1.0f - t) * theta) / sinTheta;
    float wb = std::sin(t * theta) / sinTheta;
    
    return a * wa + b * wb;
}

// Compute VBAP gains for a direction by injecting a unit sample
// This uses AlloLib's VBAP implementation to get the speaker gains
void VBAPRenderer::computeVBAPGains(const al::Vec3f& dir, std::vector<float>& gains) {
    int numSpeakers = mLayout.speakers.size();
    gains.resize(numSpeakers, 0.0f);
    
    // Use a small AudioIOData buffer with a unit sample to extract gains
    al::AudioIOData tempAudio;
    tempAudio.framesPerBuffer(1);
    tempAudio.framesPerSecond(mSpatial.sampleRate);
    tempAudio.channelsIn(0);
    tempAudio.channelsOut(numSpeakers);
    tempAudio.zeroOut();
    
    // Render a single unit sample at the given direction
    float unitSample = 1.0f;
    tempAudio.frame(0);
    mVBAP.renderBuffer(tempAudio, dir, &unitSample, 1);
    
    // Extract gains from output channels
    tempAudio.frame(0);
    for (int ch = 0; ch < numSpeakers; ch++) {
        gains[ch] = tempAudio.out(ch, 0);
    }
}

// Main safe direction getter - wraps interpolation with fallback logic
al::Vec3f VBAPRenderer::safeDirForSource(const std::string& name, 
                                          const std::vector<Keyframe>& kfs, 
                                          double t) {
    // Get raw interpolated direction
    al::Vec3f v = interpolateDirRaw(kfs, t);
    float m2 = v.magSqr();
    
    // Check for degenerate direction
    if (!finite3(v) || !std::isfinite(m2) || m2 < 1e-8f) {
        // Increment fallback counter
        mFallbackCount[name]++;
        
        // Warn once per source
        if (mWarnedDegenerate.find(name) == mWarnedDegenerate.end()) {
            std::cerr << "Warning: degenerate direction for source '" << name 
                      << "' at t=" << t << "s";
            if (!kfs.empty()) {
                std::cerr << " (keyframes: " << kfs.size() 
                          << ", first=[" << kfs.front().x << "," << kfs.front().y << "," << kfs.front().z << "]"
                          << " at t=" << kfs.front().time << ")";
            }
            std::cerr << ", using last-good/fallback\n";
            mWarnedDegenerate.insert(name);
        }
        
        // Try last-good direction for this source
        auto it = mLastGoodDir.find(name);
        if (it != mLastGoodDir.end()) {
            return it->second;
        }
        
        // Global fallback: front direction
        return al::Vec3f(0.0f, 1.0f, 0.0f);
    }
    
    // Valid direction - normalize and store as last-good
    al::Vec3f normalized = v.normalize();
    mLastGoodDir[name] = normalized;
    return normalized;
}

// Raw interpolation - may return invalid vectors (caller must validate)
al::Vec3f VBAPRenderer::interpolateDirRaw(const std::vector<Keyframe> &kfs, double t) {
    // Returns interpolated Cartesian direction from keyframes.
    // Does NOT validate output - use safeDirForSource() for safe access.
    
    // Empty keyframes - return zero vector (will trigger fallback)
    if (kfs.empty()) {
        return al::Vec3f(0.0f, 0.0f, 0.0f);
    }
    
    // Single keyframe - return its direction directly
    if (kfs.size() == 1) {
        return al::Vec3f(kfs[0].x, kfs[0].y, kfs[0].z);
    }
    
    // Clamp to first keyframe if before all keyframes
    if (t <= kfs.front().time) {
        return al::Vec3f(kfs.front().x, kfs.front().y, kfs.front().z);
    }
    
    // Clamp to last keyframe if after all keyframes
    if (t >= kfs.back().time) {
        return al::Vec3f(kfs.back().x, kfs.back().y, kfs.back().z);
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
        return al::Vec3f(k2->x, k2->y, k2->z);
    }
    
    // Linear interpolation
    double u = (t - k1->time) / dt;
    u = std::max(0.0, std::min(1.0, u));  // clamp u to [0,1] for safety
    
    return al::Vec3f(
        (1.0 - u) * k1->x + u * k2->x,
        (1.0 - u) * k1->y + u * k2->y,
        (1.0 - u) * k1->z + u * k2->z
    );
}

// Print end-of-render fallback summary
void VBAPRenderer::printFallbackSummary(int totalBlocks) {
    if (mFallbackCount.empty()) {
        std::cout << "  Direction fallbacks: none (all sources had valid directions)\n";
        return;
    }
    
    // Sort sources by fallback count (descending)
    std::vector<std::pair<std::string, int>> sorted(mFallbackCount.begin(), mFallbackCount.end());
    std::sort(sorted.begin(), sorted.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::cout << "  Direction fallbacks by source:\n";
    int totalFallbacks = 0;
    for (const auto& [name, count] : sorted) {
        float pct = 100.0f * count / totalBlocks;
        std::cout << "    " << name << ": " << count << " blocks (" 
                  << std::fixed << std::setprecision(1) << pct << "%)\n";
        totalFallbacks += count;
        
        // Copy to stats
        mLastStats.sourceFallbackCount[name] = count;
    }
    mLastStats.totalFallbackBlocks = totalFallbacks;
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
    
    // Reset per-render tracking state
    resetPerRenderState();
    
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
    std::cout << "  Render resolution: " << config.renderResolution << " (block size: " << config.blockSize << ")\n";
    
    if (!config.soloSource.empty()) {
        std::cout << "  SOLO MODE: Only rendering source '" << config.soloSource << "'\n";
    }
    if (config.t0 >= 0.0 || config.t1 >= 0.0) {
        std::cout << "  TIME WINDOW: " << (config.t0 >= 0.0 ? config.t0 : 0.0) 
                  << "s to " << (config.t1 >= 0.0 ? config.t1 : durationSec) << "s\n";
    }

    // output uses consecutive channels 0 to numSpeakers-1
    MultiWavData out;
    out.sampleRate = sr;
    out.channels = numSpeakers;
    out.samples.resize(numSpeakers);
    for (auto &c : out.samples) c.resize(renderSamples, 0.0f);

    // Dispatch to appropriate render resolution
    if (config.renderResolution == "block") {
        renderPerBlock(out, config, startSample, endSample);
    } else if (config.renderResolution == "sample") {
        renderPerSample(out, config, startSample, endSample);
    } else {
        // Default to "smooth" mode
        renderSmooth(out, config, startSample, endSample);
    }
    
    // Calculate total blocks for fallback summary
    int totalBlocks = (renderSamples + config.blockSize - 1) / config.blockSize;
    
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
    
    // Print fallback summary (shows which sources had degenerate directions)
    printFallbackSummary(totalBlocks);
    
    // Write statistics to JSON if diagnostics enabled
    if (config.debugDiagnostics) {
        fs::create_directories(config.debugOutputDir);
        std::ofstream statsFile(config.debugOutputDir + "/render_stats.json");
        statsFile << "{\n";
        statsFile << "  \"totalSamples\": " << mLastStats.totalSamples << ",\n";
        statsFile << "  \"durationSec\": " << mLastStats.durationSec << ",\n";
        statsFile << "  \"numChannels\": " << mLastStats.numChannels << ",\n";
        statsFile << "  \"numSources\": " << mLastStats.numSources << ",\n";
        statsFile << "  \"renderResolution\": \"" << config.renderResolution << "\",\n";
        statsFile << "  \"blockSize\": " << config.blockSize << ",\n";
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

// renderPerBlock: Direction computed once per block (fastest, may have stepping artifacts)
void VBAPRenderer::renderPerBlock(MultiWavData &out, const RenderConfig &config,
                                   size_t startSample, size_t endSample) {
    int sr = mSpatial.sampleRate;
    int numSpeakers = mLayout.speakers.size();
    int bufferSize = config.blockSize;
    size_t renderSamples = endSample - startSample;
    
    // Initialize AudioIOData for VBAP
    al::AudioIOData audioIO;
    audioIO.framesPerBuffer(bufferSize);
    audioIO.framesPerSecond(sr);
    audioIO.channelsIn(0);
    audioIO.channelsOut(numSpeakers);
    
    std::vector<float> sourceBuffer(bufferSize, 0.0f);
    
    int blocksProcessed = 0;
    for (size_t blockStart = startSample; blockStart < endSample; blockStart += bufferSize) {
        size_t blockEnd = std::min(endSample, blockStart + bufferSize);
        size_t blockLen = blockEnd - blockStart;
        size_t outBlockStart = blockStart - startSample;
        
        if (blocksProcessed % 1000 == 0) {
            std::cout << "  Block " << blocksProcessed << " (" 
                      << (int)(100.0 * (blockStart - startSample) / renderSamples) << "%)\n" << std::flush;
        }
        blocksProcessed++;
        
        audioIO.zeroOut();
        
        for (auto &[name, kfs] : mSpatial.sources) {
            if (!config.soloSource.empty() && name != config.soloSource) continue;
            
            auto srcIt = mSources.find(name);
            if (srcIt == mSources.end()) continue;
            const MonoWavData &src = srcIt->second;
            
            std::fill(sourceBuffer.begin(), sourceBuffer.end(), 0.0f);
            for (size_t i = 0; i < blockLen; i++) {
                size_t globalIdx = blockStart + i;
                sourceBuffer[i] = (globalIdx < src.samples.size()) ? src.samples[globalIdx] : 0.0f;
            }
            
            // Direction computed once at block start
            double timeSec = (double)blockStart / (double)sr;
            al::Vec3f dir = safeDirForSource(name, kfs, timeSec);
            
            audioIO.frame(0);
            mVBAP.renderBuffer(audioIO, dir, sourceBuffer.data(), blockLen);
        }
        
        // Copy output with gain
        audioIO.frame(0);
        for (size_t i = 0; i < blockLen; i++) {
            for (int ch = 0; ch < numSpeakers; ch++) {
                float sample = audioIO.out(ch, i);
                if (!std::isfinite(sample)) sample = 0.0f;
                out.samples[ch][outBlockStart + i] = sample * config.masterGain;
            }
        }
    }
}

// renderSmooth: Direction interpolated within each block using SLERP
void VBAPRenderer::renderSmooth(MultiWavData &out, const RenderConfig &config,
                                 size_t startSample, size_t endSample) {
    int sr = mSpatial.sampleRate;
    int numSpeakers = mLayout.speakers.size();
    int bufferSize = config.blockSize;
    size_t renderSamples = endSample - startSample;
    
    // For smooth mode, we need per-sample gain interpolation
    // We compute VBAP gains at block start and end, then interpolate
    std::vector<float> gainsStart(numSpeakers);
    std::vector<float> gainsEnd(numSpeakers);
    std::vector<float> gainsInterp(numSpeakers);
    
    int blocksProcessed = 0;
    for (size_t blockStart = startSample; blockStart < endSample; blockStart += bufferSize) {
        size_t blockEnd = std::min(endSample, blockStart + bufferSize);
        size_t blockLen = blockEnd - blockStart;
        size_t outBlockStart = blockStart - startSample;
        
        if (blocksProcessed % 1000 == 0) {
            std::cout << "  Block " << blocksProcessed << " (" 
                      << (int)(100.0 * (blockStart - startSample) / renderSamples) << "%)\n" << std::flush;
        }
        blocksProcessed++;
        
        // For each source, compute interpolated gains across the block
        for (auto &[name, kfs] : mSpatial.sources) {
            if (!config.soloSource.empty() && name != config.soloSource) continue;
            
            auto srcIt = mSources.find(name);
            if (srcIt == mSources.end()) continue;
            const MonoWavData &src = srcIt->second;
            
            // Get directions at block boundaries
            double timeStart = (double)blockStart / (double)sr;
            double timeEnd = (double)blockEnd / (double)sr;
            
            al::Vec3f dirStart = safeDirForSource(name, kfs, timeStart);
            al::Vec3f dirEnd = safeDirForSource(name, kfs, timeEnd);
            
            // Compute VBAP gains at both ends
            computeVBAPGains(dirStart, gainsStart);
            computeVBAPGains(dirEnd, gainsEnd);
            
            // Process each sample with interpolated gains
            for (size_t i = 0; i < blockLen; i++) {
                size_t globalIdx = blockStart + i;
                float inputSample = (globalIdx < src.samples.size()) ? src.samples[globalIdx] : 0.0f;
                
                // Interpolate gains within block
                float t = (blockLen > 1) ? (float)i / (float)(blockLen - 1) : 0.0f;
                for (int ch = 0; ch < numSpeakers; ch++) {
                    gainsInterp[ch] = gainsStart[ch] * (1.0f - t) + gainsEnd[ch] * t;
                }
                
                // Accumulate into output
                for (int ch = 0; ch < numSpeakers; ch++) {
                    float sample = inputSample * gainsInterp[ch] * config.masterGain;
                    if (!std::isfinite(sample)) sample = 0.0f;
                    out.samples[ch][outBlockStart + i] += sample;
                }
            }
        }
    }
}

// renderPerSample: Direction computed at every sample (slowest, smoothest)
void VBAPRenderer::renderPerSample(MultiWavData &out, const RenderConfig &config,
                                    size_t startSample, size_t endSample) {
    int sr = mSpatial.sampleRate;
    int numSpeakers = mLayout.speakers.size();
    size_t renderSamples = endSample - startSample;
    
    std::vector<float> gains(numSpeakers);
    
    // Progress reporting interval
    size_t reportInterval = renderSamples / 100;
    if (reportInterval < 1000) reportInterval = 1000;
    
    size_t samplesProcessed = 0;
    for (size_t sampleIdx = startSample; sampleIdx < endSample; sampleIdx++) {
        size_t outIdx = sampleIdx - startSample;
        
        if (samplesProcessed % reportInterval == 0) {
            std::cout << "  Sample " << samplesProcessed << "/" << renderSamples 
                      << " (" << (int)(100.0 * samplesProcessed / renderSamples) << "%)\n" << std::flush;
        }
        samplesProcessed++;
        
        double timeSec = (double)sampleIdx / (double)sr;
        
        for (auto &[name, kfs] : mSpatial.sources) {
            if (!config.soloSource.empty() && name != config.soloSource) continue;
            
            auto srcIt = mSources.find(name);
            if (srcIt == mSources.end()) continue;
            const MonoWavData &src = srcIt->second;
            
            float inputSample = (sampleIdx < src.samples.size()) ? src.samples[sampleIdx] : 0.0f;
            
            // Compute direction at exact sample time
            al::Vec3f dir = safeDirForSource(name, kfs, timeSec);
            computeVBAPGains(dir, gains);
            
            // Accumulate into output
            for (int ch = 0; ch < numSpeakers; ch++) {
                float sample = inputSample * gains[ch] * config.masterGain;
                if (!std::isfinite(sample)) sample = 0.0f;
                out.samples[ch][outIdx] += sample;
            }
        }
    }
}
