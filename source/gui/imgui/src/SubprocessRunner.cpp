#include "SubprocessRunner.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <fcntl.h>
#  include <io.h>
#  include <windows.h>
#else
#  include <cerrno>
#  include <spawn.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
extern char** environ;
#endif

namespace {
#ifdef _WIN32
std::wstring wideFromUtf8OrAnsi(const std::string& input) {
    if (input.empty()) return {};

    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), -1, nullptr, 0);
    if (len > 0) {
        std::wstring out(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), -1, out.data(), len);
        out.resize(static_cast<size_t>(len - 1));
        return out;
    }

    len = MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, out.data(), len);
    out.resize(static_cast<size_t>(len - 1));
    return out;
}

std::wstring quoteWindowsArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";

    bool needsQuotes = false;
    for (wchar_t c : arg) {
        if (c == L' ' || c == L'\t' || c == L'"') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) return arg;

    std::wstring quoted;
    quoted.push_back(L'"');
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashes = 0;
            continue;
        }
        if (backslashes > 0) {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
        }
        quoted.push_back(c);
    }
    if (backslashes > 0) quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring buildWindowsCommandLine(const std::vector<std::string>& args) {
    std::wstring cmd;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmd.push_back(L' ');
        cmd += quoteWindowsArg(wideFromUtf8OrAnsi(args[i]));
    }
    return cmd;
}
#endif
}

SubprocessRunner::SubprocessRunner() = default;

SubprocessRunner::~SubprocessRunner() {
    if (mThread.joinable()) mThread.join();
}

bool SubprocessRunner::start(const std::vector<std::string>& cmd_and_args,
                             OutputCallback output_cb) {
    if (mRunning.load(std::memory_order_relaxed)) return false;
    if (mThread.joinable()) mThread.join();

    mRunning.store(true, std::memory_order_relaxed);
    mExitCode.store(0, std::memory_order_relaxed);

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
    if (commandAndArgs.empty()) {
        if (cb) cb("[error] Failed to launch subprocess: empty argv");
        mExitCode.store(-1, std::memory_order_relaxed);
        mRunning.store(false, std::memory_order_relaxed);
        return;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        if (cb) cb("[error] Failed to create subprocess pipe.");
        mExitCode.store(-1, std::memory_order_relaxed);
        mRunning.store(false, std::memory_order_relaxed);
        return;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = buildWindowsCommandLine(commandAndArgs);
    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');

    const std::wstring appPath = wideFromUtf8OrAnsi(commandAndArgs.front());
    const BOOL ok = CreateProcessW(
        appPath.empty() ? nullptr : appPath.c_str(),
        mutableCmd.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);

    CloseHandle(writePipe);
    if (!ok) {
        const DWORD err = GetLastError();
        if (cb) cb("[error] Failed to launch subprocess: Windows error " + std::to_string(err));
        CloseHandle(readPipe);
        mExitCode.store(-1, std::memory_order_relaxed);
        mRunning.store(false, std::memory_order_relaxed);
        return;
    }

    std::string line;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        for (DWORD i = 0; i < bytesRead; ++i) {
            const char ch = buffer[i];
            if (ch == '\r' || ch == '\n') {
                if (!line.empty() && cb) cb(line);
                line.clear();
                continue;
            }
            line.push_back(ch);
        }
    }
    if (!line.empty() && cb) cb(line);

    CloseHandle(readPipe);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    mExitCode.store(static_cast<int>(exitCode), std::memory_order_relaxed);
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

    std::fclose(pipe);
    int ret = 0;
    if (waitpid(pid, &ret, 0) < 0) {
        ret = -1;
    }
    if (ret >= 0) {
        if (WIFEXITED(ret)) ret = WEXITSTATUS(ret);
        else if (WIFSIGNALED(ret)) ret = 128 + WTERMSIG(ret);
    }
    mExitCode.store(ret, std::memory_order_relaxed);
#endif
    mRunning.store(false, std::memory_order_relaxed);
}
