#pragma once

#include <string>
#include <vector>
#include <al/sound/al_Speaker.hpp>

struct SpeakerData {
    float azimuth;
    float elevation;
    float radius;
    int deviceChannel;
};

struct subwooferData {
    int deviceChannel;
};

struct SpeakerLayoutData {
    std::vector<SpeakerData> speakers;
    std::vector<subwooferData> subwoofers;
};

class LayoutLoader {
public:
    static SpeakerLayoutData loadLayout(const std::string &path);
    static al::Speakers buildAlloSpeakers(const SpeakerLayoutData &in);

};
