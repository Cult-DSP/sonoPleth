// Spatializer.hpp — Agent 3: DBAP Spatial Audio Panning
//
// Takes per-source audio blocks from Streaming and per-source positions from
// Pose, and distributes each source's audio across the speaker array using
// AlloLib's DBAP (Distance-Based Amplitude Panning) algorithm.
//
// RESPONSIBILITIES:
// 1. Build the al::Speakers array from SpeakerLayoutData at load time.
//    - CRITICAL: al::Speaker expects degrees; layout JSON stores radians.
//    - CRITICAL: Use consecutive 0-based channel indices, NOT hardware
//      deviceChannel numbers (which have gaps and cause out-of-bounds).
// 2. Compute outputChannels from the layout (matching the offline renderer):
//    maxChannel = max(numSpeakers-1, max(subwooferDeviceChannels))
//    outputChannels = maxChannel + 1
//    This value is written into RealtimeConfig so the backend can open
//    AudioIO with the correct channel count. Nothing is hardcoded.
// 3. Create al::Dbap with the speaker array and apply focus setting.
// 4. For each audio block, spatialize every non-LFE source via renderBuffer().
// 5. Route LFE sources directly to subwoofer channels (no spatialization).
// 6. Apply master gain to all output.
//
// INTERNAL RENDER BUFFER:
//   DBAP renders into an internal AudioIOData buffer (mRenderIO) sized for
//   outputChannels (layout-derived). After rendering, channels are copied
//   to the real AudioIO output. This copy step is currently an identity
//   mapping, but in the future a Channel Remap agent will re-route logical
//   render channels to physical device outputs (e.g., mapping consecutive
//   speaker channels to non-consecutive hardware outputs like the Allosphere).
//   See channelMapping.hpp for the Allosphere-specific mapping reference.
//
// PROVENANCE:
// - Speaker construction: adapted from SpatialRenderer constructor (lines 66-73)
//   with the same radians→degrees fix and 0-based channel fix.
// - Output channel sizing: adapted from SpatialRenderer::render() (lines 837-842):
//   maxChannel = max(numSpeakers-1, max_sub_channel); out.channels = maxChannel+1
// - DBAP panning: uses al::Dbap::renderBuffer() directly (same as offline).
// - LFE routing: adapted from SpatialRenderer::renderPerBlock() (lines 1018-1028)
//   with the same subGain = masterGain * 0.95 / numSubwoofers formula.
// - Coordinate transform: already handled by Pose.hpp (direction → DBAP position).
// - Channel remap concept: adapted from channelMapping.hpp and mainplayer.hpp
//   which map render channels → hardware device outputs.
//
// REAL-TIME SAFETY:
// - renderBlock() is called on the audio thread. No allocation, no locks,
//   no I/O. All buffers are pre-allocated at init time.
// - al::Dbap::renderBuffer() is real-time safe (fixed-size arrays, no alloc).

#pragma once

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "al/io/al_AudioIO.hpp"
#include "al/sound/al_Dbap.hpp"
#include "al/sound/al_Speaker.hpp"

#include "RealtimeTypes.hpp"
#include "LayoutLoader.hpp"
#include "Streaming.hpp"
#include "Pose.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Spatializer — DBAP panning engine for the real-time pipeline
// ─────────────────────────────────────────────────────────────────────────────

class Spatializer {
public:

    Spatializer(RealtimeConfig& config, EngineState& state)
        : mConfig(config), mState(state) {}

    // ── Initialize from speaker layout ───────────────────────────────────
    // Must be called BEFORE the audio stream starts.
    // Builds the al::Speakers array, computes outputChannels from layout,
    // and creates the DBAP panner.
    //
    // CRITICAL FIX 1 (from SpatialRenderer):
    //   al::Speaker expects angles in DEGREES. The layout JSON stores radians.
    //   We must convert: degrees = radians * 180 / π
    //
    // CRITICAL FIX 2 (from SpatialRenderer):
    //   AlloSphere hardware uses non-consecutive channel numbers (1-60 with gaps).
    //   al::Dbap writes to io.outBuffer(deviceChannel), so we must use
    //   consecutive 0-based indices as the channel to avoid out-of-bounds.
    //   Hardware channel remapping happens in a later phase (Channel Remap agent).
    //
    // OUTPUT CHANNEL COMPUTATION (from SpatialRenderer::render() lines 837-842):
    //   maxChannel = max(numSpeakers - 1, max(subwoofer deviceChannels))
    //   outputChannels = maxChannel + 1
    //   This value is written into mConfig.outputChannels so the backend opens
    //   AudioIO with exactly the right number of channels for this layout.

    bool init(const SpeakerLayoutData& layout) {

        // ── Build al::Speakers with 0-based consecutive channels ─────────
        mNumSpeakers = static_cast<int>(layout.speakers.size());
        mSpeakers.clear();
        mSpeakers.reserve(mNumSpeakers);

        for (int i = 0; i < mNumSpeakers; ++i) {
            const auto& spk = layout.speakers[i];
            mSpeakers.emplace_back(al::Speaker(
                i,                                          // consecutive 0-based channel
                spk.azimuth   * 180.0f / static_cast<float>(M_PI),  // rad → deg
                spk.elevation * 180.0f / static_cast<float>(M_PI),  // rad → deg
                0,                                          // group id
                spk.radius                                  // distance from center (meters)
            ));
        }

        std::cout << "[Spatializer] Built " << mNumSpeakers
                  << " al::Speaker objects (0-based consecutive channels)." << std::endl;

        // ── Collect subwoofer hardware channels ──────────────────────────
        // LFE sources are routed directly to these channels (no DBAP).
        // These are raw deviceChannel indices from the layout JSON (same as
        // the offline renderer, which indexes out.samples[deviceChannel]).
        // The internal render buffer is sized to accommodate them.
        mSubwooferChannels.clear();
        for (const auto& sub : layout.subwoofers) {
            mSubwooferChannels.push_back(sub.deviceChannel);
        }

        std::cout << "[Spatializer] " << mSubwooferChannels.size()
                  << " subwoofer channel(s):";
        for (int ch : mSubwooferChannels) {
            std::cout << " " << ch;
        }
        std::cout << std::endl;

        // ── Compute output channel count from layout ─────────────────────
        // Matches SpatialRenderer::render() (lines 837-842):
        //   maxChannel = max(numSpeakers - 1, max(subwoofer deviceChannels))
        //   outputChannels = maxChannel + 1
        //
        // This means the output may have gap channels (e.g. Allosphere has
        // channels 13-16 and 47-48 unused). The future Channel Remap agent
        // (see channelMapping.hpp) will handle mapping these to physical
        // device outputs. For now, render channels = device channels.
        int maxChannel = mNumSpeakers - 1;
        for (int subCh : mSubwooferChannels) {
            if (subCh > maxChannel) maxChannel = subCh;
        }
        int computedOutputChannels = maxChannel + 1;

        // Write into config so the backend opens AudioIO with the right count
        mConfig.outputChannels = computedOutputChannels;

        std::cout << "[Spatializer] Output channels derived from layout: "
                  << computedOutputChannels
                  << " (speakers: 0-" << (mNumSpeakers - 1)
                  << ", max sub ch: " << maxChannel << ")." << std::endl;

        // ── Create DBAP panner ───────────────────────────────────────────
        mDBap = std::make_unique<al::Dbap>(mSpeakers, mConfig.dbapFocus);
        std::cout << "[Spatializer] DBAP initialized (focus="
                  << mConfig.dbapFocus << ")." << std::endl;

        // ── Pre-allocate per-source mono buffer ──────────────────────────
        mSourceBuffer.resize(mConfig.bufferSize, 0.0f);

        // ── Pre-allocate internal render buffer ──────────────────────────
        // DBAP renders into this buffer (sized to outputChannels).
        // After rendering, channels are copied to the real AudioIO output.
        //
        // WHY: al::Dbap::renderBuffer() writes to io.outBuffer(channel)
        // using the 0-based consecutive channel indices we assigned to
        // speakers, plus subwoofer deviceChannels. The render buffer must
        // be large enough for all of these.
        //
        // The copy step after rendering is currently an identity mapping.
        // In the future, the Channel Remap agent will re-route logical
        // render channels to physical hardware outputs here. This is the
        // same pattern as channelMapping.hpp / mainplayer.hpp where the
        // ADM player maps file channels to Allosphere output channels.
        mRenderIO.framesPerBuffer(mConfig.bufferSize);
        mRenderIO.framesPerSecond(mConfig.sampleRate);
        mRenderIO.channelsIn(0);
        mRenderIO.channelsOut(computedOutputChannels);

        std::cout << "[Spatializer] Internal render buffer: "
                  << computedOutputChannels << " channels × "
                  << mConfig.bufferSize << " frames." << std::endl;

        mInitialized = true;
        return true;
    }

    // ── Render one audio block ───────────────────────────────────────────
    // Called from processBlock() on the audio thread.
    //
    // All rendering happens into the internal mRenderIO buffer:
    //   - Non-LFE sources → DBAP spatialize into speaker channels
    //   - LFE sources → route directly to subwoofer channels
    //
    // After rendering, channels are copied from mRenderIO to the real
    // AudioIO output. This copy step is the future Channel Remap point.
    // Currently it's an identity mapping (render ch N → device ch N).
    //
    // io output buffers must be zeroed BEFORE calling this method.
    //
    // REAL-TIME SAFE: no allocation, no I/O, no locks.

    void renderBlock(al::AudioIOData& io,
                     Streaming& streaming,
                     const std::vector<SourcePose>& poses,
                     uint64_t currentFrame,
                     unsigned int numFrames) {

        if (!mInitialized) return;

        const float masterGain = mConfig.masterGain.load(std::memory_order_relaxed);
        const unsigned int renderChannels = mRenderIO.channelsOut();

        // Zero the internal render buffer (DBAP accumulates into it)
        mRenderIO.zeroOut();

        for (const auto& pose : poses) {

            // Skip sources with no valid position
            if (!pose.isValid) continue;

            // ── LFE routing (no spatialization) ──────────────────────────
            // Adapted from SpatialRenderer::renderPerBlock() lines 1018-1028
            // subGain = masterGain * 0.95 / numSubwoofers
            // LFE writes into the render buffer (same as non-LFE).
            // The remap step will later handle routing to physical outputs.
            if (pose.isLFE) {
                if (mSubwooferChannels.empty()) continue;

                // Read LFE audio into pre-allocated buffer
                streaming.getBlock(pose.name, currentFrame, numFrames,
                                   mSourceBuffer.data());

                float subGain = (masterGain * kSubCompensation)
                                / static_cast<float>(mSubwooferChannels.size());

                for (int subCh : mSubwooferChannels) {
                    // Bounds check against render buffer
                    if (static_cast<unsigned int>(subCh) >= renderChannels) continue;

                    float* out = mRenderIO.outBuffer(subCh);
                    for (unsigned int f = 0; f < numFrames; ++f) {
                        out[f] += mSourceBuffer[f] * subGain;
                    }
                }
                continue;
            }

            // ── DBAP spatialization ──────────────────────────────────────
            // Read mono audio from streaming agent
            streaming.getBlock(pose.name, currentFrame, numFrames,
                               mSourceBuffer.data());

            // Apply master gain to the source buffer before DBAP.
            for (unsigned int f = 0; f < numFrames; ++f) {
                mSourceBuffer[f] *= masterGain;
            }

            // Spatialize into the internal render buffer (sized for all
            // layout channels). renderBuffer() accumulates (+=) into
            // mRenderIO output buffers.
            mDBap->renderBuffer(mRenderIO, pose.position,
                                mSourceBuffer.data(), numFrames);
        }

        // ── Copy render buffer to real output (Channel Remap point) ──────
        // Currently identity mapping: render channel N → device channel N.
        // Only copy channels that exist in both the render buffer and the
        // real AudioIO output (they should be the same size since both are
        // derived from the layout, but we guard against mismatch).
        //
        // FUTURE: The Channel Remap agent will replace this loop with a
        // mapping table (like channelMapping.hpp's defaultChannelMap),
        // routing logical render channels to physical hardware outputs.
        const unsigned int numOutputChannels = io.channelsOut();
        const unsigned int copyChannels = std::min(renderChannels, numOutputChannels);

        for (unsigned int ch = 0; ch < copyChannels; ++ch) {
            const float* src = mRenderIO.outBuffer(ch);
            float* dst = io.outBuffer(ch);
            for (unsigned int f = 0; f < numFrames; ++f) {
                dst[f] += src[f];
            }
        }
    }

    // ── Accessors ────────────────────────────────────────────────────────
    int numSpeakers() const { return mNumSpeakers; }
    bool isInitialized() const { return mInitialized; }

private:

    // ── Constants ────────────────────────────────────────────────────────
    // LFE/subwoofer compensation factor (same as offline renderer).
    // TODO: Make configurable or derive from DBAP focus setting.
    static constexpr float kSubCompensation = 0.95f;

    // ── References ───────────────────────────────────────────────────────
    RealtimeConfig& mConfig;
    EngineState&    mState;

    // ── DBAP state ───────────────────────────────────────────────────────
    al::Speakers                mSpeakers;          // AlloLib speaker objects (0-based channels)
    std::unique_ptr<al::Dbap>   mDBap;              // DBAP panner instance
    int                         mNumSpeakers = 0;   // Number of main speakers
    std::vector<int>            mSubwooferChannels; // Subwoofer channel indices (from layout)
    bool                        mInitialized = false;

    // ── Internal render buffer (sized for layout-derived outputChannels) ──
    // DBAP and LFE both render into this buffer. The copy step in
    // renderBlock() is the future Channel Remap insertion point.
    al::AudioIOData             mRenderIO;

    // ── Pre-allocated audio buffer (one source at a time) ────────────────
    std::vector<float>          mSourceBuffer;
};
