#pragma once

#include "OfflineRender.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class OfflineRenderJob {
public:
    OfflineRenderJob() = default;
    ~OfflineRenderJob();

    bool start(OfflineRenderOptions options);
    void requestCancel();
    bool isRunning() const { return mRunning.load(std::memory_order_relaxed); }
    bool cancelRequested() const { return mCancelRequested.load(std::memory_order_relaxed); }
    void wait();

    OfflineRenderProgress latestProgress() const;
    std::vector<std::string> consumeLogLines();
    std::optional<OfflineRenderResult> takeFinishedResult();

private:
    void workerMain(OfflineRenderOptions options);

    mutable std::mutex mMutex;
    std::thread mThread;
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mCancelRequested{false};
    OfflineRenderProgress mProgress;
    std::vector<std::string> mLogLines;
    std::optional<OfflineRenderResult> mFinishedResult;
};
