#include "OfflineOutputRouteMap.hpp"

#include <algorithm>
#include <ostream>
#include <set>
#include <sstream>

bool OfflineOutputRouteMap::valid() const {
    return errors.empty();
}

namespace {

std::string joinChannels(const std::vector<int> &channels) {
    if (channels.empty()) {
        return "none";
    }

    std::ostringstream oss;
    for (size_t i = 0; i < channels.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << channels[i];
    }
    return oss.str();
}

void appendChannelValidation(std::vector<std::string> &errors,
                             std::vector<std::string> &warnings,
                             std::set<int> &usedOutputChannels,
                             int deviceChannel,
                             const std::string &label) {
    if (deviceChannel < 0) {
        errors.push_back(label + " has negative deviceChannel (" + std::to_string(deviceChannel) + ").");
        return;
    }

    if (!usedOutputChannels.insert(deviceChannel).second) {
        errors.push_back("Duplicate deviceChannel " + std::to_string(deviceChannel) + " assigned to " + label + ".");
    }

    if (deviceChannel > 127) {
        warnings.push_back(label + " has suspiciously large deviceChannel " + std::to_string(deviceChannel) + ".");
    }
}

} // namespace

OfflineOutputRouteMap buildOfflineOutputRouteMap(const SpeakerLayoutData &layout) {
    OfflineOutputRouteMap routeMap;
    routeMap.mainSpeakerCount = static_cast<int>(layout.speakers.size());
    routeMap.subwooferCount = static_cast<int>(layout.subwoofers.size());
    routeMap.internalChannelCount = routeMap.mainSpeakerCount + routeMap.subwooferCount;

    if (layout.speakers.empty()) {
        routeMap.errors.push_back("Speaker layout has no main speakers.");
    }

    std::set<int> usedOutputChannels;
    int maxOutputChannel = -1;

    routeMap.routes.reserve(routeMap.internalChannelCount);

    for (int i = 0; i < routeMap.mainSpeakerCount; ++i) {
        const int deviceChannel = layout.speakers[i].deviceChannel;
        const std::string label = "speaker " + std::to_string(i);
        appendChannelValidation(routeMap.errors, routeMap.warnings, usedOutputChannels, deviceChannel, label);

        if (deviceChannel >= 0) {
            maxOutputChannel = std::max(maxOutputChannel, deviceChannel);
        }

        routeMap.routes.push_back({i, deviceChannel, label, false});
    }

    std::vector<int> mainOutputChannels;
    mainOutputChannels.reserve(routeMap.mainSpeakerCount);
    for (const auto &route : routeMap.routes) {
        if (!route.isSubwoofer && route.outputChannel >= 0) {
            mainOutputChannels.push_back(route.outputChannel);
        }
    }

    const auto mainMinMax = std::minmax_element(mainOutputChannels.begin(), mainOutputChannels.end());
    const bool hasMainOutputRange = mainMinMax.first != mainOutputChannels.end();

    for (int j = 0; j < routeMap.subwooferCount; ++j) {
        const int deviceChannel = layout.subwoofers[j].deviceChannel;
        const std::string label = "subwoofer " + std::to_string(j);
        appendChannelValidation(routeMap.errors, routeMap.warnings, usedOutputChannels, deviceChannel, label);

        if (deviceChannel >= 0) {
            maxOutputChannel = std::max(maxOutputChannel, deviceChannel);
        }

        routeMap.routes.push_back({routeMap.mainSpeakerCount + j, deviceChannel, label, true});

        if (deviceChannel >= 0 && hasMainOutputRange) {
            const int minMain = *mainMinMax.first;
            const int maxMain = *mainMinMax.second;
            if (deviceChannel < minMain || deviceChannel > maxMain) {
                routeMap.warnings.push_back(
                    label + " deviceChannel " + std::to_string(deviceChannel) +
                    " sits outside the main speaker deviceChannel range [" +
                    std::to_string(minMain) + ", " + std::to_string(maxMain) + "].");
            } else {
                routeMap.warnings.push_back(
                    label + " deviceChannel " + std::to_string(deviceChannel) +
                    " sits inside the main speaker deviceChannel range [" +
                    std::to_string(minMain) + ", " + std::to_string(maxMain) + "].");
            }
        }
    }

    if (maxOutputChannel >= 0) {
        routeMap.outputChannelCount = maxOutputChannel + 1;
    }

    routeMap.activeOutputChannels.assign(usedOutputChannels.begin(), usedOutputChannels.end());

    if (!routeMap.activeOutputChannels.empty()) {
        for (int outputChannel = 0; outputChannel < routeMap.outputChannelCount; ++outputChannel) {
            if (!usedOutputChannels.count(outputChannel)) {
                routeMap.silentOutputChannels.push_back(outputChannel);
            }
        }
    }

    if (!routeMap.silentOutputChannels.empty()) {
        routeMap.warnings.push_back(
            "Non-contiguous device channels detected. Unmapped output channels will be silent.");
    }

    return routeMap;
}

void printOfflineOutputRouteMap(std::ostream &os, const OfflineOutputRouteMap &routeMap) {
    os << "Offline Output Route Map\n";
    os << "Main speakers: " << routeMap.mainSpeakerCount << "\n";
    os << "Subwoofers: " << routeMap.subwooferCount << "\n";
    os << "Internal channels: " << routeMap.internalChannelCount << "\n";
    os << "Output channels: " << routeMap.outputChannelCount << "\n";
    os << "Active output channels: " << joinChannels(routeMap.activeOutputChannels) << "\n";
    os << "Silent output channels: " << joinChannels(routeMap.silentOutputChannels) << "\n\n";

    os << "Routes:\n";
    if (routeMap.routes.empty()) {
        os << "  none\n";
    } else {
        for (const auto &route : routeMap.routes) {
            os << "  internal " << route.internalChannel << " "
               << (route.isSubwoofer ? "subwoofer " : "speaker ")
               << "\"" << route.label << "\" -> output " << route.outputChannel << "\n";
        }
    }

    os << "\nWarnings:\n";
    if (routeMap.warnings.empty()) {
        os << "  none\n";
    } else {
        for (const auto &warning : routeMap.warnings) {
            os << "  " << warning << "\n";
        }
    }

    os << "\nErrors:\n";
    if (routeMap.errors.empty()) {
        os << "  none\n";
    } else {
        for (const auto &error : routeMap.errors) {
            os << "  " << error << "\n";
        }
    }
}
