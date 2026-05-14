#include "LayoutLoader.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

#include "SndFileHelpers.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

SpeakerLayoutData LayoutLoader::loadLayout(const std::string &path) {
    const fs::path fsPath = spatialroot::pathFromString(path);
    std::ifstream f(fsPath, std::ios::binary);
    if (!f.good()) throw std::runtime_error("Cannot open layout JSON: " + path);

    json j;
    try {
        j = json::parse(f, nullptr, true, true);
    } catch (const json::parse_error& e) {
        throw std::runtime_error(std::string("Failed to parse layout JSON: ") + e.what());
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("Invalid layout JSON: ") + e.what());
    }

    if (!j.is_object()) {
        throw std::runtime_error("Layout JSON root must be an object.");
    }
    if (!j.contains("speakers") || !j["speakers"].is_array()) {
        throw std::runtime_error("Layout JSON must contain a 'speakers' array.");
    }
    if (j["speakers"].empty()) {
        throw std::runtime_error("Layout JSON 'speakers' array is empty.");
    }
    if (j.contains("subwoofers") && !j["subwoofers"].is_array()) {
        throw std::runtime_error("Layout JSON field 'subwoofers' must be an array when present.");
    }

    SpeakerLayoutData d;

    for (size_t i = 0; i < j["speakers"].size(); ++i) {
        auto &s = j["speakers"][i];
        if (!s.is_object()) {
            throw std::runtime_error("Layout JSON speakers[" + std::to_string(i) + "] must be an object.");
        }
        if (!s.contains("az") || !s["az"].is_number()) {
            throw std::runtime_error("Layout JSON speakers[" + std::to_string(i) + "].az must be numeric.");
        }
        if (!s.contains("el") || !s["el"].is_number()) {
            throw std::runtime_error("Layout JSON speakers[" + std::to_string(i) + "].el must be numeric.");
        }
        if (!s.contains("radius") || !s["radius"].is_number()) {
            throw std::runtime_error("Layout JSON speakers[" + std::to_string(i) + "].radius must be numeric.");
        }
        if (!s.contains("channel") || !s["channel"].is_number_integer()) {
            throw std::runtime_error("Layout JSON speakers[" + std::to_string(i) + "].channel must be an integer.");
        }
        SpeakerData spk;
        spk.azimuth      = s["az"];
        spk.elevation    = s["el"];
        spk.radius       = s["radius"];
        spk.deviceChannel= s["channel"];
        d.speakers.push_back(spk);
    }

    if (j.contains("subwoofers")) {
        for (size_t i = 0; i < j["subwoofers"].size(); ++i) {
            auto &s = j["subwoofers"][i];
            if (!s.is_object()) {
                throw std::runtime_error("Layout JSON subwoofers[" + std::to_string(i) + "] must be an object.");
            }
            if (!s.contains("channel") || !s["channel"].is_number_integer()) {
                throw std::runtime_error("Layout JSON subwoofers[" + std::to_string(i) + "].channel must be an integer.");
            }
            subwooferData sub;
            sub.deviceChannel = s["channel"];
            d.subwoofers.push_back(sub);
        }
    }

    return d;
}
