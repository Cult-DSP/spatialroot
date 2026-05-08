#pragma once
// SubprocessRunner — cross-platform background subprocess with line-buffered output.
//
// Runs a subprocess (e.g. cult-transcoder) on a background thread and streams
// its stdout+stderr (merged via 2>&1) to an OutputCallback, one line at a time.
//
// Thread safety: OutputCallback is called from the background thread.
// The caller is responsible for protecting any shared state the callback touches
// (e.g., lock a mutex before appending to a shared log deque).
//
// Usage:
//   SubprocessRunner runner;
//   runner.start({"cult-transcoder", "transcode", "--in", "file.wav", ...},
//                [](const std::string& line) { ... });
//   while (runner.isRunning()) { /* poll */ }
//   int code = runner.exitCode();

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

class SubprocessRunner {
public:
    using OutputCallback = std::function<void(const std::string& line)>;

    SubprocessRunner();
    ~SubprocessRunner();  // joins the thread if still running

    // Launch the subprocess. cmd_and_args[0] = executable path.
    // Returns false if a subprocess is already running.
    bool start(const std::vector<std::string>& cmd_and_args, OutputCallback output_cb);

    bool isRunning() const { return mRunning.load(std::memory_order_relaxed); }

    // Exit code — valid only after isRunning() returns false.
    int exitCode() const { return mExitCode.load(std::memory_order_relaxed); }

    // Block until the subprocess finishes. Returns exit code.
    int wait();

private:
    void threadFunc(std::string command, OutputCallback cb);

    std::thread           mThread;
    std::atomic<bool>     mRunning{false};
    std::atomic<int>      mExitCode{0};
};
