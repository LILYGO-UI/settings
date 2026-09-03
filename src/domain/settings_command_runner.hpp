#ifndef LILYGO_UI_SETTINGS_DOMAIN_SETTINGS_COMMAND_RUNNER_HPP
#define LILYGO_UI_SETTINGS_DOMAIN_SETTINGS_COMMAND_RUNNER_HPP

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

namespace lilygo::settings {

enum class SettingsCommand {
    none,
    wifi_scan,
    wifi_radio_sync,
    wifi_sync,
    wifi_connect,
    wifi_disconnect,
    wifi_power,
    bluetooth_scan,
    bluetooth_devices,
    bluetooth_sync,
    bluetooth_connect,
    bluetooth_power,
};

[[nodiscard]] bool settings_command_is_background(SettingsCommand command) noexcept;

struct SettingsCommandResult {
    SettingsCommand command = SettingsCommand::none;
    bool success            = false;
    std::string output;
};

class SettingsCommandRunner {
public:
    SettingsCommandRunner() = default;
    ~SettingsCommandRunner();

    SettingsCommandRunner(const SettingsCommandRunner &)            = delete;
    SettingsCommandRunner &operator=(const SettingsCommandRunner &) = delete;
    SettingsCommandRunner(SettingsCommandRunner &&)                 = delete;
    SettingsCommandRunner &operator=(SettingsCommandRunner &&)      = delete;

    [[nodiscard]] bool start(SettingsCommand command, const std::vector<std::string> &arguments,
                             std::string_view standard_input   = {},
                             std::chrono::milliseconds timeout = std::chrono::seconds(30));
    [[nodiscard]] std::optional<SettingsCommandResult> poll();
    void stop() noexcept;

    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] SettingsCommand active_command() const noexcept;

private:
    [[nodiscard]] std::string read_output() const;
    void reset() noexcept;

    pid_t pid_               = 0;
    SettingsCommand command_ = SettingsCommand::none;
    std::string output_path_;
    std::chrono::steady_clock::time_point deadline_{};
};

}  // namespace lilygo::settings

#endif
