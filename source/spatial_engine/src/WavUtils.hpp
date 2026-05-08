#pragma once

#include <string>
#include <vector>
#include <map>

struct MonoWavData {
    int sampleRate;
    std::vector<float> samples;
};

struct MultiWavData {
    int sampleRate;
    int channels;
    std::vector<std::vector<float>> samples;
};

class WavUtils {
public:
    static std::map<std::string, MonoWavData>
    loadSources(const std::string &folder,
                const std::map<std::string, std::vector<struct Keyframe>> &sourceKeys,
                int expectedSR);

    static std::map<std::string, MonoWavData>
    loadSourcesFromADM(const std::string &admFile,
                       const std::map<std::string, std::vector<struct Keyframe>> &sourceKeys,
                       int expectedSR);

    static void writeMultichannelWav(const std::string &path,
                                     const MultiWavData &mw);
};
