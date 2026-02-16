#include "WavUtils.hpp"
#include <sndfile.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

MonoWavData loadMonoFile(const fs::path &path) {
    SF_INFO info;
    SNDFILE *snd = sf_open(path.string().c_str(), SFM_READ, &info);
    if (!snd) throw std::runtime_error("Failed to open WAV: " + path.string());

    if (info.channels != 1)
        throw std::runtime_error("Source WAV is not mono: " + path.string());

    MonoWavData d;
    d.sampleRate = info.samplerate;
    d.samples.resize(info.frames);

    sf_read_float(snd, d.samples.data(), info.frames);
    sf_close(snd);

    return d;
}

std::map<std::string, MonoWavData>
WavUtils::loadSources(const std::string &folder,
                      const std::map<std::string, std::vector<struct Keyframe>> &sourceKeys,
                      int expectedSR)
{
    std::map<std::string, MonoWavData> out;

    for (auto &[name, kf] : sourceKeys) {
        fs::path p = fs::path(folder) / (name + ".wav");

        if (!fs::exists(p)) {
            throw std::runtime_error("Missing source WAV: " + p.string());
        }

        MonoWavData d = loadMonoFile(p);

        if (d.sampleRate != expectedSR) {
            throw std::runtime_error("Sample rate mismatch in: " + p.string());
        }

        out[name] = d;
    }

    return out;
}

void WavUtils::writeMultichannelWav(const std::string &path,
                                    const MultiWavData &mw)
{
    SF_INFO info = {};
    info.channels = mw.channels;
    info.samplerate = mw.sampleRate;

    // Auto-select RF64 when audio data exceeds the standard WAV 4 GB limit.
    // Standard WAV uses unsigned 32-bit data-chunk sizes (max ~4.29 GB).
    // RF64 (EBU Tech 3306) is the broadcast-standard extension with 64-bit sizes.
    // libsndfile supports RF64 natively — readers that support RF64 include
    // libsndfile, ffmpeg, SoX, Audacity, Reaper, and most DAWs.
    size_t dataSizeBytes = (size_t)mw.samples[0].size() * mw.channels * sizeof(float);
    constexpr size_t kWavMaxBytes = 0xFFFFFFFF;  // ~4.29 GB unsigned 32-bit limit

    if (dataSizeBytes > kWavMaxBytes) {
        info.format = SF_FORMAT_RF64 | SF_FORMAT_FLOAT;
        std::cout << "NOTE: Using RF64 format (data size "
                  << dataSizeBytes / (1024 * 1024) << " MB exceeds WAV 4 GB limit)\n";
    } else {
        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    }

    double durationSec = (double)mw.samples[0].size() / mw.sampleRate;
    std::cout << "Writing " << (dataSizeBytes > kWavMaxBytes ? "RF64" : "WAV")
              << ": " << mw.channels << " channels, "
              << mw.sampleRate << " Hz, "
              << durationSec << " seconds ("
              << mw.samples[0].size() << " samples/ch, "
              << dataSizeBytes / (1024 * 1024) << " MB)\n";

    SNDFILE *snd = sf_open(path.c_str(), SFM_WRITE, &info);
    if (!snd) {
        std::cerr << "Error opening file for write: " << sf_strerror(nullptr) << "\n";
        throw std::runtime_error("Cannot create WAV file");
    }

    size_t totalSamples = mw.samples[0].size();
    std::vector<float> interleaved(totalSamples * mw.channels);

    std::cout << "Interleaving " << interleaved.size() << " total samples...\n";

    for (size_t i = 0; i < totalSamples; i++) {
        for (int ch = 0; ch < mw.channels; ch++) {
            interleaved[i * mw.channels + ch] = mw.samples[ch][i];
        }
    }

    std::cout << "Writing to file...\n";
    sf_count_t written = sf_write_float(snd, interleaved.data(), interleaved.size());
    std::cout << "Wrote " << written << " samples (expected " << interleaved.size() << ")\n";
    
    if (written != (sf_count_t)interleaved.size()) {
        std::cerr << "Write error: " << sf_strerror(snd) << "\n";
    }
    
    sf_close(snd);
    std::cout << "File closed\n";
}
