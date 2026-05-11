#include "../src/OfflineRenderRemapPlan.hpp"

#include <cstdlib>
#include <iostream>

int main() {
    SpeakerLayoutData layout;
    layout.speakers.push_back({0.0f, 0.0f, 1.0f, 0});
    layout.speakers.push_back({1.0f, 0.0f, 1.0f, 1});
    layout.speakers.push_back({2.0f, 0.0f, 1.0f, 12});
    layout.subwoofers.push_back({15});

    const OfflineRenderRemapPlan plan = OfflineRenderRemapPlan::build(layout);
    if (plan.internalChannelCount() != 4) {
        std::cerr << "Expected 4 internal channels.\n";
        return EXIT_FAILURE;
    }
    if (plan.outputChannelCount() != 16) {
        std::cerr << "Expected 16 output channels.\n";
        return EXIT_FAILURE;
    }

    MultiWavData internal;
    internal.sampleRate = 48000;
    internal.channels = 4;
    internal.samples = {
        {1.0f, 0.0f},
        {2.0f, 0.0f},
        {3.0f, 0.0f},
        {4.0f, 0.0f}
    };

    const MultiWavData output = plan.scatterToDeviceIndexed(internal);
    if (output.channels != 16) {
        std::cerr << "Scatter output channel count mismatch.\n";
        return EXIT_FAILURE;
    }
    if (output.samples[0][0] != 1.0f ||
        output.samples[1][0] != 2.0f ||
        output.samples[12][0] != 3.0f ||
        output.samples[15][0] != 4.0f) {
        std::cerr << "Mapped channels do not contain the expected samples.\n";
        return EXIT_FAILURE;
    }
    for (int ch : {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14}) {
        if (output.samples[static_cast<size_t>(ch)][0] != 0.0f) {
            std::cerr << "Expected silent gap channel at index " << ch << ".\n";
            return EXIT_FAILURE;
        }
    }

    std::cout << "offline_render_plan_test PASS\n";
    return EXIT_SUCCESS;
}
