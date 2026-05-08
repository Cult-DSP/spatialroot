#include "SubprocessRunner.hpp"

#include <array>
#include <cstdio>
#include <sstream>

#ifdef _WIN32
#  define POPEN  _popen
#  define PCLOSE _pclose
#else
#  include <sys/wait.h>
#  define POPEN  popen
#  define PCLOSE pclose
#endif

// ── Command quoting ───────────────────────────────────────────────────────────
// Build a shell-safe quoted command string from a token list.
// Each token is wrapped in double quotes; internal double quotes are backslash-
// escaped. stderr is merged into stdout with "2>&1" so the caller sees all
// output through a single pipe.
static std::string buildCommand(const std::vector<std::string>& tokens) {
    std::ostringstream cmd;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) cmd << ' ';
        cmd << '"';
        for (char c : tokens[i]) {
            if (c == '"') cmd << '\\';
            cmd << c;
        }
        cmd << '"';
    }
    cmd << " 2>&1";
    return cmd.str();
}

// ── SubprocessRunner ──────────────────────────────────────────────────────────

SubprocessRunner::SubprocessRunner() = default;

SubprocessRunner::~SubprocessRunner() {
    if (mThread.joinable()) mThread.join();
}

bool SubprocessRunner::start(const std::vector<std::string>& cmd_and_args,
                              OutputCallback output_cb) {
    if (mRunning.load(std::memory_order_relaxed)) return false;
    if (mThread.joinable()) mThread.join();  // clean up previous run

    std::string command = buildCommand(cmd_and_args);
    mRunning.store(true,  std::memory_order_relaxed);
    mExitCode.store(0,    std::memory_order_relaxed);

    mThread = std::thread([this, command, output_cb]() {
        threadFunc(command, output_cb);
    });

    return true;
}

int SubprocessRunner::wait() {
    if (mThread.joinable()) mThread.join();
    return mExitCode.load(std::memory_order_relaxed);
}

void SubprocessRunner::threadFunc(std::string command, OutputCallback cb) {
    FILE* pipe = POPEN(command.c_str(), "r");
    if (!pipe) {
        if (cb) cb("[error] Failed to launch subprocess");
        mExitCode.store(-1, std::memory_order_relaxed);
        mRunning.store(false, std::memory_order_relaxed);
        return;
    }

    std::array<char, 1024> buf;
    std::string line;
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
        line = buf.data();
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (!line.empty() && cb) cb(line);
    }

    int ret = PCLOSE(pipe);
#ifndef _WIN32
    // pclose() returns the raw waitpid() status — extract the actual exit code.
    if (WIFEXITED(ret))   ret = WEXITSTATUS(ret);
    else if (WIFSIGNALED(ret)) ret = 128 + WTERMSIG(ret);
#endif
    mExitCode.store(ret,   std::memory_order_relaxed);
    mRunning.store(false,  std::memory_order_relaxed);
}
