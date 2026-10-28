#include "pages/sharing/sharing_model.hpp"

#include <cstdlib>
#include <sstream>
#include <utility>

namespace lilygo::settings {
namespace {

struct UnitState {
    std::string id;
    std::string load;
    std::string active;
    std::string file;

    bool valid() const
    {
        return (load == "loaded" || load == "masked" || load == "not-found") &&
               (active == "active" || active == "inactive" || active == "failed" || active == "activating" ||
                active == "deactivating" || active == "reloading") &&
               (load != "loaded" || !file.empty());
    }
    bool running() const
    {
        return active == "active" || active == "reloading";
    }
    bool transitioning() const
    {
        return active == "activating" || active == "deactivating";
    }
    bool starts_at_boot() const
    {
        return file == "enabled";
    }
};

}  // namespace

SharingEnvironment SharingEnvironment::from_process_environment()
{
    const char *command = std::getenv("LILYGO_UI_SYSTEMCTL");
    const char *sudo = std::getenv("LILYGO_UI_SUDO");
    return {command && command[0] ? command : "/usr/bin/systemctl",
            sudo && sudo[0] ? sudo : "/usr/bin/sudo"};
}

SharingModel::SharingModel(SharingEnvironment environment) : environment_(std::move(environment))
{
}

bool SharingModel::update(std::string_view output)
{
    UnitState service, socket, current;
    const auto finish = [&] {
        if (current.id == "ssh.service") service = current;
        if (current.id == "ssh.socket") socket = current;
        current = {};
    };
    std::istringstream input{std::string(output)};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) {
            finish();
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const auto key   = line.substr(0, separator);
        const auto value = line.substr(separator + 1);
        if (key == "Id")
            current.id = value;
        else if (key == "LoadState")
            current.load = value;
        else if (key == "ActiveState")
            current.active = value;
        else if (key == "UnitFileState")
            current.file = value;
    }
    finish();
    if (!service.valid() || !socket.valid()) {
        refresh_failed();
        return false;
    }
    socket_loaded_        = socket.load == "loaded";
    state_.known          = true;
    state_.available      = service.load != "not-found";
    state_.enabled        = service.running() || socket.running();
    state_.masked         = service.load == "masked" || service.file == "masked" || service.file == "masked-runtime";
    state_.transitioning  = service.transitioning() || socket.transitioning();
    state_.starts_at_boot = service.starts_at_boot() || socket.starts_at_boot();
    return true;
}

void SharingModel::refresh_failed() noexcept
{
    state_.known = false;
}
const SharingState &SharingModel::state() const noexcept
{
    return state_;
}

std::vector<std::string> SharingModel::query_arguments() const
{
    return {environment_.systemctl,
            "--no-pager",
            "--no-ask-password",
            "show",
            "--all",
            "--property=Id,LoadState,ActiveState,UnitFileState",
            "ssh.service",
            "ssh.socket"};
}

std::vector<std::string> SharingModel::change_arguments(bool enabled) const
{
    if (!state_.known || !state_.available || state_.masked || state_.transitioning) return {};
    // The package authorizes only these exact SSH commands for administrator users.
    // Never prompt for or store a password in the application.
    std::vector<std::string> arguments;
    if (!environment_.sudo.empty()) arguments = {environment_.sudo, "-n", "--"};
    arguments.insert(arguments.end(), {environment_.systemctl, "--no-pager", "--no-ask-password",
                                        enabled ? "enable" : "disable", "--now"});
    // Stop the listener too, so socket activation cannot reopen remote login.
    if (!enabled && socket_loaded_) arguments.emplace_back("ssh.socket");
    arguments.emplace_back("ssh.service");
    return arguments;
}

}  // namespace lilygo::settings
