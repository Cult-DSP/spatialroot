#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "../src/LayoutLoader.hpp"

struct OfflineOutputRoute {
    int internalChannel = -1;
    int outputChannel = -1;
    std::string label;
    bool isSubwoofer = false;
};

struct OfflineOutputRouteMap {
    int mainSpeakerCount = 0;
    int subwooferCount = 0;
    int internalChannelCount = 0;
    int outputChannelCount = 0;

    std::vector<OfflineOutputRoute> routes;
    std::vector<int> activeOutputChannels;
    std::vector<int> silentOutputChannels;

    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    bool valid() const;
};

OfflineOutputRouteMap buildOfflineOutputRouteMap(const SpeakerLayoutData &layout);
void printOfflineOutputRouteMap(std::ostream &os, const OfflineOutputRouteMap &routeMap);
