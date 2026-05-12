#include "SubprocessRunner.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#  include <sstream>
#  define POPEN  _popen
#  define PCLOSE _pclose
#else
#  include <cerrno>
#  include <spawn.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
extern char** environ;
#endif

// ── SubprocessRunner ──────────────────────────────────────────────────────────

SubprocessRunner::SubprocessRunner() = default;

SubprocessRunner::~SubprocessRunner() {
    if (mThread.joinable()) mThread.join();
}

bool SubprocessRunner::start(const std::vector<std::string>& cmd_and_args,
                              OutputCallback output_cb) {
    if (mRunning.load(std::memory_order_relaxed)) return false;
    if (mThread.joinable()) mThread.join();  // clean up previous run

    mRunning.store(true,  std::memory_order_relaxed);
    mExitCode.store(0,    std::memory_order_relaxed);

    mThread = std::thread([this, cmd_and_args, output_cb]() {
        threadFunc(cmd_and_args, output_cb);
    });

    return true;
}

int SubprocessRunner::wait() {
    if (mThread.joinable()) mThread.join();
    return mExitCode.load(std::memory_order_relaxed);
}

void SubprocessRunner::threadFunc(std::vector<std::string> commandAndArgs, OutputCallback cb) {
#ifdef _WIN32
    std::ostringstream cmd;
    for (size_t i = 0; i < commandAndArgs.size(); ++i) {
        if (i > 0) cmd << ' ';
        cmd << '"';
        for (char c : commandAndArgs[i]) {
            if (c == '"') cmd << '\\';
            cmd << c;
        }
        cmd << '"';
    }
    cmd << " 2>&1";

    FILE* pipe = POPEN(cmd.str().c_str(), "r");
    if (!pipe) {
        if (cb) cb(std::string("[error] Failed to launch subprocess: ") + std::strerror(errno));
        mExitCode.store(-1, std::memory_order_relaxed);
        mRunning.store(false, std::memory_order_relaxed);
        return;
    }
#else
    if (commandAndArgs.empty()) {
        if (cb) cb("[error] Failed to launch subprocess: empty argv");
        mExitCode.store(-1, std::memory_order_relaxed);
        mRunning.store(false, std::memory_order_relaxed);
        return;
    }

    int pipeFds[2] = {-1, -1};
    if (pipe(pipeFds) != 0) {
        if (cb) cb(std::string("[error] Failed to create subprocess pipe: ") + std::strerror(errno));
        mExitCode.store(-1, std::memory_order_relaxed);
        mRunning.store(false, std::memory_order_relaxed);
        return;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipeFds[1]);

    std::vector<char*> argv;
    argv.reserve(commandAndArgs.size() + 1);
    for (auto& arg : commandAndArgs) argv.push_back(arg.data());
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int spawnErr = posix_spawnp(&pid, commandAndArgs.front().c_str(),
                                      &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipeFds[1]);

    if (spawnErr != 0) {
        close(pipeFds[0]);
        if (cb) cb(std::string("[error] Failed to launch subprocess: ") + std::strerror(spawnErr));
        mExitCode.store(-1, std::memory_order_relaxed);
        mRunning.store(false, std::memory_order_relaxed);
        return;
    }

    FILE* pipe = fdopen(pipeFds[0], "r");
    if (!pipe) {
        close(pipeFds[0]);
        int status = 0;
        (void)waitpid(pid, &status, 0);
        if (cb) cb(std::string("[error] Failed to read subprocess output: ") + std::strerror(errno));
        mExitCode.store(-1, std::memory_order_relaxed);
        mRunning.store(false, std::memory_order_relaxed);
        return;
    }
#endif

    std::string line;
    int ch = 0;
    while ((ch = std::fgetc(pipe)) != EOF) {
        if (ch == '\r' || ch == '\n') {
            if (!line.empty() && cb) cb(line);
            line.clear();
            continue;
        }
        line.push_back(static_cast<char>(ch));
    }
    if (!line.empty() && cb) cb(line);

#ifdef _WIN32
    int ret = PCLOSE(pipe);
#else
    std::fclose(pipe);
    int ret = 0;
    if (waitpid(pid, &ret, 0) < 0) {
        ret = -1;
    }
#endif

#ifndef _WIN32
    if (ret >= 0) {
        if (WIFEXITED(ret)) ret = WEXITSTATUS(ret);
        else if (WIFSIGNALED(ret)) ret = 128 + WTERMSIG(ret);
    }
#endif
    mExitCode.store(ret,   std::memory_order_relaxed);
    mRunning.store(false,  std::memory_order_relaxed);
}
