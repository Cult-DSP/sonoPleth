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
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

    std::cout << "Writing WAV: " << mw.channels << " channels, " 
              << mw.sampleRate << " Hz\n";
    std::cout << "Samples per channel: " << mw.samples[0].size() << "\n";

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
