#include <iostream>
#include <string>
#include <cstdlib>

struct RenderConfig {
    float masterGainDb = 0.0f; // Master gain in dB (-60–+12, 0 = unity)
};

int main(int argc, char* argv[]) {
    RenderConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--master_gain") {
            config.masterGainDb = std::stof(argv[++i]);
        }
    }

    // Output the received masterGainDb value
    std::cout << "Received masterGainDb: " << config.masterGainDb << " dB" << std::endl;

    return 0;
}