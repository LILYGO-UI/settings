#include "domain/settings_command_runner.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <csignal>
#include <string_view>

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace lilygo::settings {
namespace {

constexpr int command_shutdown_polls                = 25;
constexpr useconds_t command_shutdown_poll_delay_us = 10000;
constexpr std::size_t maximum_standard_input_size   = 4096;
constexpr rlim_t maximum_command_output_size        = 1024U * 1024U;

std::vector<std::string> command_environment(std::string &search_path)
{
    std::vector<std::string> environment;
    bool found_path = false;
    for (auto entry = environ; entry && *entry; ++entry) {
        const std::string_view value(*entry);
        if (!found_path && value.substr(0, 5) == "PATH=") {
            search_path = value.substr(5);
            found_path = true;
        }
        if (value.substr(0, 7) != "LC_ALL=") environment.emplace_back(value);
    }
    environment.emplace_back("LC_ALL=C");
    if (!found_path) {
        const auto length = confstr(_CS_PATH, nullptr, 0);
        if (length > 0) {
            search_path.resize(length);
            if (confstr(_CS_PATH, search_path.data(), length) > 0)
                search_path.resize(length - 1);
            else
                search_path = "/bin:/usr/bin";
        } else {
            search_path = "/bin:/usr/bin";
        }
    }
    return environment;
}

std::vector<std::string> command_candidates(const std::string &command, const std::string &search_path)
{
    if (command.empty() || command.find('/') != std::string::npos) return {command};
    std::vector<std::string> candidates;
    std::size_t begin = 0;
    while (true) {
        const auto end = search_path.find(':', begin);
        const auto directory = search_path.substr(begin, end == std::string::npos ? end : end - begin);
        candidates.push_back(directory.empty() ? command : directory + '/' + command);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return candidates;
}

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
           command == SettingsCommand::wifi_profiles || command == SettingsCommand::wifi_profile ||
           command == SettingsCommand::wifi_details || command == SettingsCommand::wifi_details_profile ||
           command == SettingsCommand::bluetooth_dbus_devices || command == SettingsCommand::bluetooth_paired_devices ||
           command == SettingsCommand::bluetooth_info ||
           command == SettingsCommand::bluetooth_sync ||
           command == SettingsCommand::ethernet_refresh;
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

    // Cellular I/O runs on another thread. Prepare all allocating libc/C++ work
    // before fork: the child must not inherit and enter a locked allocator or
    // environment mutex through setenv() or execvp().
    std::string search_path;
    auto environment = command_environment(search_path);
    auto candidates = command_candidates(arguments.front(), search_path);
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1U);
    for (const auto &argument : arguments) argv.push_back(const_cast<char *>(argument.c_str()));
    argv.push_back(nullptr);
    std::vector<char *> envp;
    envp.reserve(environment.size() + 1U);
    for (auto &entry : environment) envp.push_back(entry.data());
    envp.push_back(nullptr);
    std::vector<const char *> executable_paths;
    executable_paths.reserve(candidates.size() + 1U);
    for (const auto &candidate : candidates) executable_paths.push_back(candidate.c_str());
    executable_paths.push_back(nullptr);
    // Preserve execvp's shell fallback for executable text without a shebang.
    std::vector<char *> shell_argv;
    shell_argv.reserve(arguments.size() + 2U);
    shell_argv.push_back(const_cast<char *>("/bin/sh"));
    shell_argv.push_back(nullptr); // The child fills in the attempted path.
    for (std::size_t index = 1; index < arguments.size(); ++index)
        shell_argv.push_back(const_cast<char *>(arguments[index].c_str()));
    shell_argv.push_back(nullptr);
    auto *const argument_data = argv.data();
    auto *const environment_data = envp.data();
    auto *const executable_data = executable_paths.data();
    auto *const shell_arguments = shell_argv.data();

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
        for (auto executable = executable_data; *executable; ++executable) {
            execve(*executable, argument_data, environment_data);
            if (errno == ENOEXEC) {
                shell_arguments[1] = const_cast<char *>(*executable);
                execve("/bin/sh", shell_arguments, environment_data);
                _exit(127);
            }
            // Keep looking after inaccessible or missing PATH entries, as
            // execvp does. Other execution errors fail the command immediately.
            switch (errno) {
            case EACCES: case ENOENT: case ENOTDIR: case ESTALE: case ENODEV: case ETIMEDOUT:
                break;
            default:
                _exit(127);
            }
        }
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
