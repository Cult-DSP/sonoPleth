// sonoPleth Spatial Renderer for AlloSphere
// 
// renders spatial audio using Vector Base Amplitude Panning (VBAP),
// Distance-Based Amplitude Panning (DBAP), or Layer-Based Amplitude Panning (LBAP)
// takes mono source files and spatial trajectory data
// outputs multichannel WAV for the AlloSphere's speaker array
//
// key gotcha that took forever to debug:
// AlloLib expects speaker angles in degrees not radians
// so the JSON loader converts from radians to degrees when creating al::Speaker objects
// without this conversion panners silently fail and produce zero output
//
// DBAP coordinate quirk: AlloLib's DBAP internally swaps: Vec3d(pos.x, -pos.z, pos.y)
// We compensate by transforming (x,y,z) -> (x,z,-y) before passing to DBAP.
// See SpatialRenderer::directionToDBAPPosition() for details.

#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib>

#include "JSONLoader.hpp"
#include "LayoutLoader.hpp"
#include "SpatialRenderer.hpp"
#include "WavUtils.hpp"

namespace fs = std::filesystem;

void printUsage() {
    std::cout << "sonoPleth Spatial Renderer\n\n";
    std::cout << "Usage:\n"
              << "  sonoPleth_spatial_render \\\n"
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
    std::cout << "Spatializer Options:\n"
              << "  --spatializer TYPE    Spatializer: vbap, dbap, or lbap (default: dbap)\n"
              << "  --dbap_focus FLOAT    DBAP focus/rolloff exponent (default: 1.0, range: 0.2-5.0)\n"
              << "  --lbap_dispersion F   LBAP dispersion threshold (default: 0.5, range: 0.0-1.0)\n\n";
    std::cout << "General Options:\n"
              << "  --master_gain FLOAT   Master gain (default: 0.25 for headroom)\n"
              << "  --solo_source NAME    Render only the named source (for debugging)\n"
              << "  --t0 SECONDS          Start time in seconds (default: 0)\n"
              << "  --t1 SECONDS          End time in seconds (default: full duration)\n"
              << "  --render_resolution MODE  Render resolution: block or sample (default: block)\n"
              << "  --block_size N        Block size in samples (default: 64, use 256 for faster renders)\n"
              << "  --elevation_mode MODE Elevation handling: compress or clamp (default: compress)\n"
              << "  --force_2d            Force 2D mode (flatten all elevations)\n"
              << "  --debug_dir DIR       Output debug diagnostics to directory\n"
              << "  --help                Show this help message\n\n";
    std::cout << "Spatializers:\n"
              << "  dbap   - Distance-Based Amplitude Panning (DEFAULT)\n"
              << "           Works with any speaker layout, no coverage gaps\n"
              << "           --dbap_focus controls distance attenuation (higher = sharper focus)\n"
              << "  vbap   - Vector Base Amplitude Panning\n"
              << "           Best for layouts with good 3D coverage, uses speaker triplets\n"
              << "           May have coverage gaps at zenith/nadir\n"
              << "  lbap   - Layer-Based Amplitude Panning\n"
              << "           Designed for multi-ring/layer layouts (e.g., 3 elevation rings)\n"
              << "           --lbap_dispersion controls zenith/nadir signal spread\n\n";
    // DEV NOTE: Future --spatializer auto mode could detect layout type:
    // - Single ring (2D): use DBAP
    // - Multi-ring with good coverage: use LBAP
    // - Dense 3D coverage: use VBAP
    // For now, default to DBAP as safest option.
    std::cout << "Render Resolutions:\n"
              << "  block  - Direction computed at block center (RECOMMENDED)\n"
              << "           Use small blockSize (32-64) for smooth motion\n"
              << "  sample - Direction computed per sample (very slow, debugging only)\n"
              << "  smooth - DEPRECATED: may cause artifacts, use 'block' instead\n\n";
    std::cout << "Elevation Modes:\n"
              << "  compress - Map full elevation range to layout's speaker coverage (RECOMMENDED)\n"
              << "             Preserves relative height differences, no signal loss\n"
              << "  clamp    - Hard clip elevations to speaker bounds\n"
              << "             May cause 'sticking' at top/bottom\n";
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
    RenderConfig config;  // Uses sensible defaults: masterGain=0.25f, pannerType=DBAP, etc.

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
        } else if (arg == "--spatializer") {
            std::string panner = argv[++i];
            if (panner == "dbap") {
                config.pannerType = PannerType::DBAP;
            } else if (panner == "vbap") {
                config.pannerType = PannerType::VBAP;
            } else if (panner == "lbap") {
                config.pannerType = PannerType::LBAP;
            } else {
                std::cerr << "Error: unknown spatializer '" << panner << "'\n";
                std::cerr << "Valid spatializers: vbap, dbap, lbap\n";
                return 1;
            }
        } else if (arg == "--dbap_focus") {
            config.dbapFocus = std::stof(argv[++i]);
            if (config.dbapFocus < 0.2f || config.dbapFocus > 5.0f) {
                std::cerr << "Warning: --dbap_focus " << config.dbapFocus 
                          << " is outside recommended range [0.2, 5.0]\n";
            }
        } else if (arg == "--lbap_dispersion") {
            config.lbapDispersion = std::stof(argv[++i]);
            if (config.lbapDispersion < 0.0f || config.lbapDispersion > 1.0f) {
                std::cerr << "Warning: --lbap_dispersion " << config.lbapDispersion 
                          << " is outside recommended range [0.0, 1.0]\n";
            }
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
        } else if (arg == "--elevation_mode") {
            std::string mode = argv[++i];
            if (mode == "compress") {
                config.elevationMode = ElevationMode::Compress;
            } else if (mode == "clamp") {
                config.elevationMode = ElevationMode::Clamp;
            } else {
                std::cerr << "Error: unknown elevation mode '" << mode << "'\n";
                std::cerr << "Valid modes: compress, clamp\n";
                return 1;
            }
        } else if (arg == "--force_2d") {
            config.force2D = true;
        }
    }

    // layout JSON has speaker positions in radians
    // these get converted to degrees when creating al::Speaker objects in SpatialRenderer
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
    SpatialRenderer renderer(layout, spatial, sources);
    MultiWavData output = renderer.render(config);

    // output has consecutive channels 0 to numSpeakers
    // if you need AlloSphere hardware channel numbers with gaps you can remap later
    std::cout << "Writing output WAV: " << outFile << "\n";
    WavUtils::writeMultichannelWav(outFile, output);

    std::cout << "Done.\n";
    return 0;
}
