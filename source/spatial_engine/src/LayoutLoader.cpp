#include "LayoutLoader.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

SpeakerLayoutData LayoutLoader::loadLayout(const std::string &path) {
    std::ifstream f(path);
    if (!f.good()) throw std::runtime_error("Cannot open layout JSON");

    json j;
    f >> j;

    SpeakerLayoutData d;

    for (auto &s : j["speakers"]) {
        SpeakerData spk;
        spk.azimuth      = s["az"];
        spk.elevation    = s["el"];
        spk.radius       = s["radius"];
        spk.deviceChannel= s["channel"];
        d.speakers.push_back(spk);
    }

    for (auto &s : j["subwoofers"]) {
        subwooferData sub;
        sub.deviceChannel = s["channel"];
        d.subwoofers.push_back(sub);
    }

    return d;
}
