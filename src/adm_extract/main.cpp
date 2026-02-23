/**
 * sonopleth_adm_extract
 *
 * Minimal CLI tool that opens a BW64/RF64/WAV file, extracts the raw `axml`
 * chunk bytes (ADM XML), and writes them to a file.
 *
 * Usage:
 *   sonopleth_adm_extract --in <input.wav> --out <output.xml>
 *
 * Exit codes:
 *   0  success
 *   1  bad arguments
 *   2  file open error
 *   3  no axml chunk found in file
 *   4  output write error
 *
 * Depends only on libbw64 (header-only, EBU).
 * libadm is a submodule neighbour required for Track B — not used here.
 */

#include <bw64/bw64.hpp>

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

static void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " --in <input.wav> --out <output.xml>\n";
}

int main(int argc, char* argv[]) {
    std::string inPath, outPath;

    // --- Parse arguments -----------------------------------------------
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--in") == 0 && i + 1 < argc) {
            inPath = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            outPath = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0 ||
                   std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << argv[i] << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (inPath.empty() || outPath.empty()) {
        std::cerr << "ERROR: --in and --out are both required.\n";
        printUsage(argv[0]);
        return 1;
    }

    // --- Open BW64/WAV file --------------------------------------------
    std::shared_ptr<bw64::Bw64Reader> reader;
    try {
        reader = bw64::readFile(inPath);
    } catch (const std::exception& e) {
        std::cerr << "ERROR opening file '" << inPath << "': " << e.what() << "\n";
        return 2;
    }

    // --- Extract axml chunk --------------------------------------------
    auto axmlChunk = reader->axmlChunk();
    if (!axmlChunk) {
        std::cerr << "ERROR: No axml chunk found in '" << inPath << "'.\n";
        std::cerr << "       Is this a valid ADM BW64 file?\n";
        return 3;
    }

    const std::string& xmlData = axmlChunk->data();

    // --- Write output --------------------------------------------------
    std::ofstream outFile(outPath, std::ios::binary);
    if (!outFile) {
        std::cerr << "ERROR: Cannot open output file '" << outPath << "' for writing.\n";
        return 4;
    }
    outFile.write(xmlData.data(), static_cast<std::streamsize>(xmlData.size()));
    if (!outFile) {
        std::cerr << "ERROR: Write failed for '" << outPath << "'.\n";
        return 4;
    }
    outFile.close();

    std::cout << "Extracted ADM XML (" << xmlData.size() << " bytes) -> " << outPath << "\n";
    return 0;
}
