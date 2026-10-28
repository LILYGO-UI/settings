#include "pages/sharing/serial_console_model.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

namespace lilygo::settings {
namespace {
std::string environment_value(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return value && value[0] ? value : fallback;
}
}  // namespace

SerialConsoleEnvironment SerialConsoleEnvironment::from_process_environment()
{
    return {environment_value("LILYGO_UI_RASPI_CONFIG", "/usr/bin/raspi-config"),
            environment_value("LILYGO_UI_SYSTEMCTL", "/usr/bin/systemctl"),
            environment_value("LILYGO_UI_RUNNING_CMDLINE", "/proc/cmdline"),
            environment_value("LILYGO_UI_SUDO", "/usr/bin/sudo")};
}

SerialConsoleModel::SerialConsoleModel(SerialConsoleEnvironment environment) : environment_(std::move(environment))
{
}

bool SerialConsoleModel::has_serial_console(std::string_view cmdline)
{
    std::istringstream input{std::string(cmdline)};
    std::string token;
    while (input >> token) {
        if (token.rfind("console=", 0) != 0) continue;
        const auto device =
            token.substr(8, token.find(',') == std::string::npos ? std::string::npos : token.find(',') - 8);
        for (const std::string_view prefix : {"serial", "ttyAMA", "ttyS"}) {
            if (device.rfind(prefix, 0) == 0 && device.size() > prefix.size() &&
                std::all_of(device.begin() + prefix.size(), device.end(), [](char c) { return c >= '0' && c <= '9'; }))
                return true;
        }
    }
    return false;
}

bool SerialConsoleModel::update(std::string_view output)
{
    const auto first = output.find_first_not_of(" \t\r\n");
    const auto last  = output.find_last_not_of(" \t\r\n");
    if (first == std::string_view::npos || first != last || (output[first] != '0' && output[first] != '1')) {
        refresh_failed();
        return false;
    }
    // raspi-config's non-interactive getters print 0 for enabled, 1 for disabled.
    const bool enabled = output[first] == '0';
    std::ifstream input(environment_.running_cmdline);
    std::string cmdline;
    if (std::getline(input, cmdline) && !cmdline.empty()) running_enabled_ = has_serial_console(cmdline);
    // On a platform without /proc/cmdline, retain the first observed state as a
    // session baseline so a successfully changed setting still requests reboot.
    if (!running_enabled_) running_enabled_ = enabled;
    enabled_ = enabled;
    known_   = true;
    return true;
}

void SerialConsoleModel::refresh_failed() noexcept
{
    known_ = false;
}
bool SerialConsoleModel::known() const noexcept
{
    return known_;
}
bool SerialConsoleModel::enabled() const noexcept
{
    return enabled_;
}
bool SerialConsoleModel::reboot_required() const noexcept
{
    return running_enabled_ && enabled_ != *running_enabled_;
}

std::vector<std::string> SerialConsoleModel::query_arguments() const
{
    return {environment_.raspi_config, "nonint", "get_serial_cons"};
}

std::vector<std::string> SerialConsoleModel::change_arguments(bool enabled) const
{
    if (!known_) return {};
    std::vector<std::string> arguments;
    if (!environment_.sudo.empty()) arguments = {environment_.sudo, "-n", "--"};
    arguments.insert(arguments.end(), {environment_.raspi_config, "nonint", "do_serial_cons", enabled ? "0" : "1"});
    return arguments;
}

std::vector<std::string> SerialConsoleModel::reboot_arguments() const
{
    std::vector<std::string> arguments;
    if (!environment_.sudo.empty()) arguments = {environment_.sudo, "-n", "--"};
    arguments.insert(arguments.end(), {environment_.systemctl, "--no-ask-password", "reboot"});
    return arguments;
}

}  // namespace lilygo::settings
