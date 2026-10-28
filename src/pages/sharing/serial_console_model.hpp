#ifndef LILYGO_UI_SETTINGS_PAGES_SHARING_SERIAL_CONSOLE_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_SHARING_SERIAL_CONSOLE_MODEL_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lilygo::settings {

struct SerialConsoleEnvironment {
    std::string raspi_config;
    std::string systemctl;
    std::string running_cmdline;
    std::string sudo = {};
    static SerialConsoleEnvironment from_process_environment();
};

class SerialConsoleModel {
public:
    explicit SerialConsoleModel(
        SerialConsoleEnvironment environment = SerialConsoleEnvironment::from_process_environment());
    [[nodiscard]] bool update(std::string_view output);
    void refresh_failed() noexcept;
    [[nodiscard]] bool known() const noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] bool reboot_required() const noexcept;
    [[nodiscard]] std::vector<std::string> query_arguments() const;
    [[nodiscard]] std::vector<std::string> change_arguments(bool enabled) const;
    [[nodiscard]] std::vector<std::string> reboot_arguments() const;
    [[nodiscard]] static bool has_serial_console(std::string_view cmdline);

private:
    SerialConsoleEnvironment environment_;
    bool known_   = false;
    bool enabled_ = false;
    std::optional<bool> running_enabled_;
};

}  // namespace lilygo::settings

#endif
