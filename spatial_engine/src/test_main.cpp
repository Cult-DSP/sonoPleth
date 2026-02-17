#include <iostream>
#include <string>
#include <cstdlib>

struct RenderConfig {
    float masterGain = 0.5f; // Default value
};

int main(int argc, char* argv[]) {
    RenderConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--master_gain") {
            config.masterGain = std::stof(argv[++i]);
        }
    }

    // Output the received masterGain value
    std::cout << "Received masterGain: " << config.masterGain << std::endl;

    return 0;
}