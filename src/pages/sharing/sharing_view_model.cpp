#include "pages/sharing/sharing_view_model.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <utility>

namespace lilygo::settings {
namespace {

void log_sharing_failure(const char *operation, const std::string &output)
{
    std::fprintf(stderr, "[settings] %s failed: %.4096s\n", operation,
                 output.empty() ? "no command output" : output.c_str());
}

std::string ssh_change_error(const std::string &output)
{
    if (output == "Command timed out") return "Remote Login Change Timed Out";
    std::string lower = output;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto *reason : {"password is required", "permission denied", "access denied", "not allowed",
                              "authentication is required", "not in the sudoers", "no new privileges",
                              "effective uid is not 0"}) {
        if (lower.find(reason) != std::string::npos)
            return "Administrator Permission Is Required to Change Remote Login.";
    }
    return "Cannot Change Remote Login. See Application Log.";
}

}  // namespace

SharingViewModel::SharingViewModel(SharingEnvironment environment, SerialConsoleEnvironment serial_environment)
    : model_(std::move(environment)), serial_model_(std::move(serial_environment))
{
}

void SharingViewModel::refresh()
{
    if (reboot_accepted_) return;
    if (!commands_.busy()) query();
    if (!serial_commands_.busy() && !reboot_prompt_) query_serial();
}

void SharingViewModel::query()
{
    if (!commands_.start(SettingsCommand::sharing_sync, model_.query_arguments(), {}, std::chrono::seconds(4))) {
        model_.refresh_failed();
        error_ = "Cannot Check Remote Login";
        failed_request_ = requested_;
        requested_.reset();
    }
    publish();
}

void SharingViewModel::set_enabled(bool enabled)
{
    if (!presentation_.control_enabled) {
        publish();
        return;
    }
    const auto arguments = model_.change_arguments(enabled);
    if (arguments.empty()) return;
    // User input takes precedence over a periodic read, but never interrupts a write/readback.
    if (!requested_ && commands_.active_command() == SettingsCommand::sharing_sync) commands_.stop();
    error_.clear();
    failed_request_.reset();
    requested_ = enabled;
    if (!commands_.start(SettingsCommand::sharing_power, arguments)) {
        error_ = "Cannot Change Remote Login";
        failed_request_ = requested_;
        requested_.reset();
    }
    publish();
}

void SharingViewModel::query_serial()
{
    if (!serial_commands_.start(SettingsCommand::serial_console_sync, serial_model_.query_arguments(), {},
                                std::chrono::seconds(4))) {
        serial_model_.refresh_failed();
        serial_error_ = "Cannot Check Serial Console";
        serial_requested_.reset();
    }
    publish();
}

void SharingViewModel::set_serial_enabled(bool enabled)
{
    if (!presentation_.serial_control_enabled || enabled == serial_model_.enabled()) {
        publish();
        return;
    }
    if (!serial_requested_ && serial_commands_.active_command() == SettingsCommand::serial_console_sync)
        serial_commands_.stop();
    serial_error_.clear();
    presentation_.reboot_error.clear();
    serial_requested_ = enabled;
    if (!serial_commands_.start(SettingsCommand::serial_console_change, serial_model_.change_arguments(enabled))) {
        serial_error_ = "Cannot Save Serial Console";
        serial_requested_.reset();
    }
    publish();
}

void SharingViewModel::poll_serial()
{
    const auto result = serial_commands_.poll();
    if (!result) return;
    if (result->command == SettingsCommand::sharing_reboot) {
        if (result->success) {
            reboot_accepted_ = true;
            reboot_prompt_   = false;
        } else {
            log_sharing_failure("restart", result->output);
            presentation_.reboot_error = "Restart Failed. Try Again or Restart Later.";
        }
        publish();
        return;
    }
    if (result->command == SettingsCommand::serial_console_change) {
        if (!result->success) {
            log_sharing_failure("Serial Console save", result->output);
            serial_error_ = result->output == "Command timed out"
                                ? "Serial Console Change Timed Out"
                                : "Cannot Save Serial Console. Check System Permissions.";
        }
        query_serial();
        return;
    }
    if (!result->success || !serial_model_.update(result->output)) {
        log_sharing_failure("Serial Console readback", result->output);
        serial_model_.refresh_failed();
        if (serial_error_.empty()) serial_error_ = "Serial Console Unavailable. Check raspi-config.";
    } else if (serial_requested_) {
        if (serial_error_.empty()) {
            if (serial_model_.enabled() == *serial_requested_)
                reboot_prompt_ = true;
            else
                serial_error_ = "Serial Console Change Was Not Applied";
        }
    } else {
        serial_error_.clear();
    }
    serial_requested_.reset();
    publish();
}

void SharingViewModel::reboot_now()
{
    // Reboot is only reachable after a verified save and an explicit dialog choice.
    if (!reboot_prompt_ || serial_commands_.busy() || reboot_accepted_) return;
    presentation_.reboot_error.clear();
    if (!serial_commands_.start(SettingsCommand::sharing_reboot, serial_model_.reboot_arguments()))
        presentation_.reboot_error = "Cannot Start Restart. Try Again or Restart Later.";
    publish();
}

void SharingViewModel::reboot_later()
{
    if (!reboot_prompt_ || serial_commands_.busy()) return;
    reboot_prompt_ = false;
    presentation_.reboot_error.clear();
    publish();
}

void SharingViewModel::poll_command()
{
    poll_serial();
    const auto result = commands_.poll();
    if (!result) return;
    if (result->command == SettingsCommand::sharing_power) {
        if (!result->success) {
            log_sharing_failure("SSH change", result->output);
            error_ = ssh_change_error(result->output);
        }
        // Even a failed command may have partly applied. Always read back reality.
        query();
        return;
    }
    if (!result->success || !model_.update(result->output)) {
        log_sharing_failure("SSH readback", result->output);
        model_.refresh_failed();
        if (error_.empty()) error_ = "Cannot Check Remote Login. SSH Requires systemd.";
    } else {
        const auto &state = model_.state();
        const auto target = requested_ ? requested_ : failed_request_;
        if (!state.available || state.masked) {
            // A newly observed platform restriction supersedes an older change error.
            failed_request_.reset();
            error_.clear();
        } else if (target) {
            // Some command failures occur after the requested state has been applied.
            // Only a complete, stable readback may override the command error.
            if (state.available && !state.transitioning && state.enabled == *target &&
                state.starts_at_boot == *target) {
                error_.clear();
                failed_request_.reset();
            } else if (error_.empty())
                error_ = "Remote Login Change Was Not Fully Applied";
        } else {
            error_.clear();
        }
    }
    // A periodic status read must not erase an unresolved change failure.
    if (requested_ && !error_.empty()) failed_request_ = requested_;
    requested_.reset();
    publish();
}

void SharingViewModel::stop() noexcept
{
    commands_.stop();
    serial_commands_.stop();
    serial_model_.refresh_failed();
    serial_requested_.reset();
    serial_error_.clear();
    reboot_prompt_ = reboot_accepted_ = false;
    presentation_.reboot_error.clear();
    requested_.reset();
    failed_request_.reset();
    model_.refresh_failed();
    error_.clear();
}

bool SharingViewModel::busy() const noexcept
{
    return commands_.busy() || serial_commands_.busy();
}
const SharingPresentation &SharingViewModel::presentation() const noexcept
{
    return presentation_;
}
lv_subject_t *SharingViewModel::revision_subject() noexcept
{
    return revision_.get();
}

void SharingViewModel::publish()
{
    // Background reads must not remove the switches from physical-button focus navigation.
    // Keep them disabled throughout a user change, including its verifying readback.
    const bool changing_login = requested_.has_value() ||
        (commands_.busy() && commands_.active_command() != SettingsCommand::sharing_sync);
    const bool changing_serial = serial_requested_.has_value() ||
        (serial_commands_.busy() && serial_commands_.active_command() != SettingsCommand::serial_console_sync);
    const auto &state             = model_.state();
    presentation_.enabled         = state.enabled;
    presentation_.control_enabled = state.known && state.available && !state.masked && !state.transitioning &&
                                    !changing_login && !reboot_prompt_ && !reboot_accepted_;
    if (requested_ && commands_.busy())
        presentation_.status = *requested_ ? "Turning On Remote Login..." : "Turning Off Remote Login...";
    else if (!error_.empty())
        presentation_.status = error_;
    else if (!state.known)
        presentation_.status = "Checking Remote Login...";
    else if (!state.available)
        presentation_.status = "SSH Server Is Not Installed";
    else if (state.masked)
        presentation_.status = "SSH Is Disabled by System Policy";
    else if (state.transitioning)
        presentation_.status = "Remote Login Is Changing...";
    else
        presentation_.status = state.enabled ? "Remote Login: On" : "Remote Login: Off";
    presentation_.serial_enabled = serial_model_.enabled();
    presentation_.serial_control_enabled =
        serial_model_.known() && !changing_serial && !reboot_prompt_ && !reboot_accepted_;
    presentation_.reboot_required    = serial_model_.reboot_required();
    presentation_.show_reboot_prompt = reboot_prompt_;
    presentation_.reboot_busy =
        serial_commands_.active_command() == SettingsCommand::sharing_reboot || reboot_accepted_;
    if (presentation_.reboot_busy)
        presentation_.serial_status = "Restarting...";
    else if (serial_requested_)
        presentation_.serial_status = "Saving Serial Console...";
    else if (!serial_error_.empty())
        presentation_.serial_status = serial_error_;
    else if (!serial_model_.known())
        presentation_.serial_status = "Checking Serial Console...";
    else if (serial_model_.reboot_required())
        presentation_.serial_status = "Restart Required to Apply Serial Console";
    else
        presentation_.serial_status = serial_model_.enabled() ? "Serial Console: On" : "Serial Console: Off";
    revision_.publish();
}

}  // namespace lilygo::settings
