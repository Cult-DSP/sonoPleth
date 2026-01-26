// sonoPleth VBAP Renderer for AlloSphere
// 
// renders spatial audio using Vector Base Amplitude Panning
// takes mono source files and spatial trajectory data
// outputs multichannel WAV for the AlloSphere's 54-speaker array
//
// key gotcha that took forever to debug:
// AlloLib expects speaker angles in degrees not radians
// so the JSON loader converts from radians to degrees when creating al::Speaker objects
// without this conversion VBAP silently fails and produces zero output

#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib>

#include "JSONLoader.hpp"
#include "LayoutLoader.hpp"
#include "VBAPRenderer.hpp"
#include "WavUtils.hpp"

namespace fs = std::filesystem;

void printUsage() {
    std::cout << "sonoPleth VBAP Renderer\n\n";
    std::cout << "Usage:\n"
              << "  sonoPleth_vbap_render \\\n"
              << "    --layout layout.json \\\n"
              << "    --positions spatial.json \\\n"
              << "    --sources <folder> \\\n"
              << "    --out output.wav \\\n"
              << "    [OPTIONS]\n\n";
    std::cout << "Required:\n"
              << "  --layout FILE       Speaker layout JSON file\n"
              << "  --positions FILE    Spatial trajectory JSON file\n"
              << "  --sources FOLDER    Folder containing mono source WAVs\n"
              << "  --out FILE          Output multichannel WAV file\n\n";
    std::cout << "Options:\n"
              << "  --master_gain FLOAT   Master gain (default: 0.5 for headroom)\n"
              << "  --solo_source NAME    Render only the named source (for debugging)\n"
              << "  --t0 SECONDS          Start time in seconds (default: 0)\n"
              << "  --t1 SECONDS          End time in seconds (default: full duration)\n"
              << "  --render_resolution MODE  Render resolution: block or sample (default: block)\n"
              << "  --block_size N        Block size in samples (default: 64, use 256 for faster renders)\n"
              << "  --debug_dir DIR       Output debug diagnostics to directory\n"
              << "  --help                Show this help message\n\n";
    std::cout << "Render Resolutions:\n"
              << "  block  - Direction computed at block center (RECOMMENDED)\n"
              << "           Use small blockSize (32-64) for smooth motion\n"
              << "  sample - Direction computed per sample (very slow, debugging only)\n"
              << "  smooth - DEPRECATED: may cause artifacts, use 'block' instead\n";
}

int main(int argc, char *argv[]) {

    // parse command line args
    // old version used positional args which was error prone
    // switched to flagged args for clarity
    if (argc < 9) {
        printUsage();
        return 1;
    }

    fs::path layoutFile, positionsFile, sourcesFolder, outFile;
    RenderConfig config;  // Uses sensible defaults: masterGain=0.25f, etc.

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else if (arg == "--layout") {
            layoutFile = argv[++i];
        } else if (arg == "--positions") {
            positionsFile = argv[++i];
        } else if (arg == "--sources") {
            sourcesFolder = argv[++i];
        } else if (arg == "--out") {
            outFile = argv[++i];
        } else if (arg == "--master_gain") {
            config.masterGain = std::stof(argv[++i]);
        } else if (arg == "--solo_source") {
            config.soloSource = argv[++i];
        } else if (arg == "--t0") {
            config.t0 = std::stod(argv[++i]);
        } else if (arg == "--t1") {
            config.t1 = std::stod(argv[++i]);
        } else if (arg == "--debug_dir") {
            config.debugDiagnostics = true;
            config.debugOutputDir = argv[++i];
        } else if (arg == "--render_resolution") {
            std::string res = argv[++i];
            if (res == "block" || res == "smooth" || res == "sample") {
                config.renderResolution = res;
            } else {
                std::cerr << "Error: unknown render resolution '" << res << "'\n";
                std::cerr << "Valid resolutions: block, smooth, sample\n";
                return 1;
            }
        } else if (arg == "--block_size") {
            config.blockSize = std::stoi(argv[++i]);
            if (config.blockSize < 1 || config.blockSize > 8192) {
                std::cerr << "Error: block_size must be between 1 and 8192\n";
                return 1;
            }
        }
    }

    // layout JSON has speaker positions in radians
    // these get converted to degrees when creating al::Speaker objects in VBAPRenderer
    std::cout << "Loading layout...\n";
    SpeakerLayoutData layout = LayoutLoader::loadLayout(layoutFile);

    // spatial trajectories with keyframes for each source
    std::cout << "Loading spatial instructions...\n";
    SpatialData spatial = JSONLoader::loadSpatialInstructions(positionsFile);

    // load all mono source files
    std::cout << "Loading source WAVs...\n";
    std::map<std::string, MonoWavData> sources =
        WavUtils::loadSources(sourcesFolder, spatial.sources, spatial.sampleRate);

    // main rendering happens here
    // this is where the degrees conversion and channel mapping fixes are critical
    std::cout << "Rendering...\n";
    VBAPRenderer renderer(layout, spatial, sources);
    MultiWavData output = renderer.render(config);

    // output has consecutive channels 0 to 53
    // if you need AlloSphere hardware channel numbers with gaps you can remap later
    std::cout << "Writing output WAV: " << outFile << "\n";
    WavUtils::writeMultichannelWav(outFile, output);

    std::cout << "Done.\n";
    return 0;
}
