#include "OfflineRenderRemapPlan.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
bool isIdentityPlan(const std::vector<OfflineRenderRoute>& routes,
                    int internalChannelCount,
                    int outputChannelCount) {
    if (internalChannelCount != outputChannelCount) return false;
    if (static_cast<int>(routes.size()) != internalChannelCount) return false;

    std::vector<bool> covered(static_cast<size_t>(internalChannelCount), false);
    for (const auto& route : routes) {
        if (route.internalChannel != route.deviceChannel) return false;
        if (route.internalChannel < 0 || route.internalChannel >= internalChannelCount) return false;
        if (covered[static_cast<size_t>(route.internalChannel)]) return false;
        covered[static_cast<size_t>(route.internalChannel)] = true;
    }

    for (bool hit : covered) {
        if (!hit) return false;
    }
    return true;
}
}

OfflineRenderRemapPlan OfflineRenderRemapPlan::build(const SpeakerLayoutData& layout) {
    OfflineRenderRemapPlan plan;
    plan.mSpeakerCount = static_cast<int>(layout.speakers.size());
    plan.mSubwooferCount = static_cast<int>(layout.subwoofers.size());

    if (plan.mSpeakerCount == 0) {
        throw std::runtime_error("Speaker layout has no speakers.");
    }

    std::set<int> speakerDeviceChannels;
    std::set<int> subwooferDeviceChannels;
    int maxDeviceChannel = -1;

    for (int i = 0; i < plan.mSpeakerCount; ++i) {
        const int deviceChannel = layout.speakers[static_cast<size_t>(i)].deviceChannel;
        if (deviceChannel < 0) {
            throw std::runtime_error("Speaker " + std::to_string(i) +
                                     " has negative deviceChannel (" +
                                     std::to_string(deviceChannel) + ").");
        }
        if (!speakerDeviceChannels.insert(deviceChannel).second) {
            throw std::runtime_error("Duplicate speaker deviceChannel " +
                                     std::to_string(deviceChannel) + ".");
        }
        if (deviceChannel > 127) {
            plan.mWarnings.push_back("Speaker " + std::to_string(i) +
                                     " uses suspiciously large deviceChannel " +
                                     std::to_string(deviceChannel) + ".");
        }
        maxDeviceChannel = std::max(maxDeviceChannel, deviceChannel);
        plan.mRoutes.push_back({i, deviceChannel, false});
    }

    for (int j = 0; j < plan.mSubwooferCount; ++j) {
        const int deviceChannel = layout.subwoofers[static_cast<size_t>(j)].deviceChannel;
        if (deviceChannel < 0) {
            throw std::runtime_error("Subwoofer " + std::to_string(j) +
                                     " has negative deviceChannel (" +
                                     std::to_string(deviceChannel) + ").");
        }
        if (!subwooferDeviceChannels.insert(deviceChannel).second) {
            throw std::runtime_error("Duplicate subwoofer deviceChannel " +
                                     std::to_string(deviceChannel) + ".");
        }
        if (speakerDeviceChannels.count(deviceChannel) != 0) {
            throw std::runtime_error("Speaker and subwoofer share deviceChannel " +
                                     std::to_string(deviceChannel) + ".");
        }
        if (deviceChannel > 127) {
            plan.mWarnings.push_back("Subwoofer " + std::to_string(j) +
                                     " uses suspiciously large deviceChannel " +
                                     std::to_string(deviceChannel) + ".");
        }
        maxDeviceChannel = std::max(maxDeviceChannel, deviceChannel);
        plan.mRoutes.push_back({plan.mSpeakerCount + j, deviceChannel, true});
    }

    plan.mInternalChannelCount = plan.mSpeakerCount + plan.mSubwooferCount;
    plan.mOutputChannelCount = maxDeviceChannel + 1;
    plan.mIdentity = isIdentityPlan(plan.mRoutes,
                                    plan.mInternalChannelCount,
                                    plan.mOutputChannelCount);
    return plan;
}

MultiWavData OfflineRenderRemapPlan::scatterToDeviceIndexed(const MultiWavData& internal) const {
    if (internal.channels != mInternalChannelCount) {
        throw std::runtime_error("Internal render channel count mismatch. Expected " +
                                 std::to_string(mInternalChannelCount) + ", got " +
                                 std::to_string(internal.channels) + ".");
    }

    MultiWavData output;
    output.sampleRate = internal.sampleRate;
    output.channels = mOutputChannelCount;
    output.samples.assign(static_cast<size_t>(mOutputChannelCount), {});

    const size_t frames = internal.samples.empty() ? 0 : internal.samples.front().size();
    for (auto& channel : output.samples) {
        channel.assign(frames, 0.0f);
    }

    for (const auto& route : mRoutes) {
        output.samples[static_cast<size_t>(route.deviceChannel)] =
            internal.samples[static_cast<size_t>(route.internalChannel)];
    }

    return output;
}

std::string OfflineRenderRemapPlan::summary() const {
    std::ostringstream oss;
    oss << mInternalChannelCount << " internal -> "
        << mOutputChannelCount << " device-indexed output channels";
    if (mIdentity) {
        oss << " (identity)";
    } else {
        oss << " (scatter with silent gaps allowed)";
    }
    return oss.str();
}
