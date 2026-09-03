#include "domain/settings_command_runner.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <csignal>
#include <string_view>

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace lilygo::settings {
namespace {

constexpr int command_shutdown_polls                = 25;
constexpr useconds_t command_shutdown_poll_delay_us = 10000;
constexpr std::size_t maximum_standard_input_size   = 4096;
constexpr rlim_t maximum_command_output_size        = 1024U * 1024U;

bool write_all(int descriptor, std::string_view input)
{
    std::size_t offset = 0;
    while (offset < input.size()) {
        const ssize_t count = write(descriptor, input.data() + offset, input.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

}  // namespace

bool settings_command_is_background(SettingsCommand command) noexcept
{
    return command == SettingsCommand::wifi_radio_sync || command == SettingsCommand::wifi_sync ||
           command == SettingsCommand::bluetooth_sync;
}

SettingsCommandRunner::~SettingsCommandRunner()
{
    stop();
}

bool SettingsCommandRunner::start(SettingsCommand command, const std::vector<std::string> &arguments,
                                  std::string_view standard_input, std::chrono::milliseconds timeout)
{
    if (busy() || command == SettingsCommand::none || arguments.empty() ||
        standard_input.size() > maximum_standard_input_size || timeout <= std::chrono::milliseconds::zero())
        return false;

    int input_pipe[2] = {-1, -1};
    if (!standard_input.empty()) {
        if (pipe(input_pipe) != 0) return false;
        const bool input_written = write_all(input_pipe[1], standard_input);
        close(input_pipe[1]);
        input_pipe[1] = -1;
        if (!input_written) {
            close(input_pipe[0]);
            return false;
        }
    }

    char path[]      = "/tmp/lilygo-ui-settings-XXXXXX";
    const int output = mkstemp(path);
    if (output < 0) {
        if (input_pipe[0] >= 0) close(input_pipe[0]);
        return false;
    }

    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1U);
    for (const auto &argument : arguments) {
        argv.push_back(const_cast<char *>(argument.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t child = fork();
    if (child < 0) {
        if (input_pipe[0] >= 0) close(input_pipe[0]);
        close(output);
        unlink(path);
        return false;
    }
    if (child == 0) {
        if (setpgid(0, 0) != 0) _exit(126);
        const struct rlimit output_limit = {maximum_command_output_size, maximum_command_output_size};
        if (setrlimit(RLIMIT_FSIZE, &output_limit) != 0) _exit(126);
        if (input_pipe[0] >= 0) {
            if (input_pipe[0] != STDIN_FILENO && dup2(input_pipe[0], STDIN_FILENO) < 0) _exit(126);
            if (input_pipe[0] > STDERR_FILENO) close(input_pipe[0]);
        }
        if (output != STDOUT_FILENO && dup2(output, STDOUT_FILENO) < 0) _exit(126);
        if (output != STDERR_FILENO && dup2(output, STDERR_FILENO) < 0) _exit(126);
        if (output > STDERR_FILENO) close(output);
        execvp(argv.front(), argv.data());
        _exit(127);
    }

    (void)setpgid(child, child);
    if (input_pipe[0] >= 0) close(input_pipe[0]);
    close(output);
    pid_         = child;
    command_     = command;
    output_path_ = path;
    deadline_    = std::chrono::steady_clock::now() + timeout;
    return true;
}

std::optional<SettingsCommandResult> SettingsCommandRunner::poll()
{
    if (!busy()) return std::nullopt;

    int status = 0;
    pid_t result;
    do {
        result = waitpid(pid_, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == 0) {
        if (std::chrono::steady_clock::now() < deadline_) return std::nullopt;
        const SettingsCommand timed_out_command = command_;
        stop();
        return SettingsCommandResult{timed_out_command, false, "Command timed out"};
    }

    SettingsCommandResult completed;
    completed.command = command_;
    completed.success = result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    completed.output  = read_output();
    (void)kill(-pid_, SIGKILL);
    reset();
    return completed;
}

void SettingsCommandRunner::stop() noexcept
{
    const pid_t child = pid_;
    if (child <= 0) {
        reset();
        return;
    }

    (void)kill(-child, SIGTERM);
    for (int poll_index = 0; poll_index < command_shutdown_polls; ++poll_index) {
        pid_t result;
        do {
            result = waitpid(child, nullptr, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result == child || (result < 0 && errno == ECHILD)) {
            (void)kill(-child, SIGKILL);
            reset();
            return;
        }
        if (result < 0) break;
        usleep(command_shutdown_poll_delay_us);
    }

    (void)kill(-child, SIGKILL);
    while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {
    }
    reset();
}

bool SettingsCommandRunner::busy() const noexcept
{
    return pid_ > 0;
}

SettingsCommand SettingsCommandRunner::active_command() const noexcept
{
    return command_;
}

std::string SettingsCommandRunner::read_output() const
{
    if (output_path_.empty()) return {};
    FILE *file = std::fopen(output_path_.c_str(), "rb");
    if (!file) return {};

    std::string output;
    char buffer[4096];
    while (output.size() < maximum_command_output_size) {
        const std::size_t remaining = static_cast<std::size_t>(maximum_command_output_size) - output.size();
        const std::size_t count     = std::fread(buffer, 1, std::min(sizeof(buffer), remaining), file);
        if (count == 0) break;
        output.append(buffer, count);
    }
    std::fclose(file);
    return output;
}

void SettingsCommandRunner::reset() noexcept
{
    if (!output_path_.empty()) unlink(output_path_.c_str());
    pid_     = 0;
    command_ = SettingsCommand::none;
    output_path_.clear();
    deadline_ = {};
}

}  // namespace lilygo::settings
