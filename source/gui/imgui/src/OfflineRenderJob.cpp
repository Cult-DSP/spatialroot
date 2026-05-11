#include "OfflineRenderJob.hpp"

#include <utility>

OfflineRenderJob::~OfflineRenderJob() {
    requestCancel();
    wait();
}

bool OfflineRenderJob::start(OfflineRenderOptions options) {
    if (mRunning.load(std::memory_order_relaxed)) return false;
    wait();

    mCancelRequested.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mProgress = {};
        mLogLines.clear();
        mFinishedResult.reset();
    }

    options.cancelFlag = &mCancelRequested;
    options.progressCallback = [this](const OfflineRenderProgress& progress) {
        std::lock_guard<std::mutex> lock(mMutex);
        mProgress = progress;
        if (!progress.message.empty()) {
            mLogLines.push_back("[" + std::string(OfflineRenderRunner::stageName(progress.stage)) +
                                "] " + progress.message);
        }
    };

    mRunning.store(true, std::memory_order_relaxed);
    mThread = std::thread([this, options = std::move(options)]() mutable {
        workerMain(std::move(options));
    });
    return true;
}

void OfflineRenderJob::requestCancel() {
    mCancelRequested.store(true, std::memory_order_relaxed);
}

void OfflineRenderJob::wait() {
    if (mThread.joinable()) mThread.join();
}

OfflineRenderProgress OfflineRenderJob::latestProgress() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mProgress;
}

std::vector<std::string> OfflineRenderJob::consumeLogLines() {
    std::lock_guard<std::mutex> lock(mMutex);
    std::vector<std::string> out;
    out.swap(mLogLines);
    return out;
}

std::optional<OfflineRenderResult> OfflineRenderJob::takeFinishedResult() {
    std::lock_guard<std::mutex> lock(mMutex);
    auto result = mFinishedResult;
    mFinishedResult.reset();
    return result;
}

void OfflineRenderJob::workerMain(OfflineRenderOptions options) {
    OfflineRenderResult result = OfflineRenderRunner::run(options);
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mFinishedResult = std::move(result);
    }
    mRunning.store(false, std::memory_order_relaxed);
}
