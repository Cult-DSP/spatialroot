#pragma once

#include "LayoutLoader.hpp"
#include "WavUtils.hpp"

#include <string>
#include <vector>

struct OfflineRenderRoute {
    int internalChannel = -1;
    int deviceChannel = -1;
    bool isSubwoofer = false;
};

class OfflineRenderRemapPlan {
public:
    static OfflineRenderRemapPlan build(const SpeakerLayoutData& layout);

    int speakerCount() const { return mSpeakerCount; }
    int subwooferCount() const { return mSubwooferCount; }
    int internalChannelCount() const { return mInternalChannelCount; }
    int outputChannelCount() const { return mOutputChannelCount; }
    bool identity() const { return mIdentity; }

    const std::vector<OfflineRenderRoute>& routes() const { return mRoutes; }
    const std::vector<std::string>& warnings() const { return mWarnings; }

    MultiWavData scatterToDeviceIndexed(const MultiWavData& internal) const;
    std::string summary() const;

private:
    int mSpeakerCount = 0;
    int mSubwooferCount = 0;
    int mInternalChannelCount = 0;
    int mOutputChannelCount = 0;
    bool mIdentity = false;
    std::vector<OfflineRenderRoute> mRoutes;
    std::vector<std::string> mWarnings;
};
