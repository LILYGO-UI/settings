#include "pages/keyboard/keyboard_model.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace lilygo::settings {
namespace {

std::optional<int> read_strength(const std::string &path)
{
    std::ifstream input(path);
    int value = 0;
    if (!(input >> value) || value < 0 || value > 100) return std::nullopt;
    input >> std::ws;
    if (!input.eof()) return std::nullopt;
    return value;
}

}  // namespace

KeyboardEnvironment KeyboardEnvironment::from_process_environment()
{
    const char *root = std::getenv("LILYGO_UI_HAPTICS_DRIVER_DIR");
    return {root && root[0] ? root : ""};
}

KeyboardModel::KeyboardModel(KeyboardEnvironment environment) : environment_(std::move(environment))
{
}

void KeyboardModel::refresh()
{
    strength_path_.clear();
    availability_ = KeyboardAvailability::unavailable;
    std::error_code error;
    std::vector<std::string> roots;
    if (!environment_.driver_root.empty()) roots.push_back(environment_.driver_root);
    // Keep compatibility with both the current module name and older images.
    roots.emplace_back("/sys/bus/i2c/drivers/drv260x-haptics");
    roots.emplace_back("/sys/bus/i2c/drivers/drv260x");
    roots.emplace_back("/sys/bus/i2c/devices");
    std::vector<std::string> paths;
    for (const auto &root : roots) {
        error.clear();
        for (std::filesystem::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
            paths.push_back((it->path() / "keyboard_feedback_strength").string());
        }
    }
    std::sort(paths.begin(), paths.end());
    for (const auto &path : paths) {
        const auto value = read_strength(path);
        if (!value) continue;
        strength_path_ = path;
        strength_      = *value;
        availability_ = access(path.c_str(), W_OK) == 0 ? KeyboardAvailability::ready : KeyboardAvailability::read_only;
        return;
    }
}

bool KeyboardModel::set_strength(int percent)
{
    if (percent < 0 || percent > 100) return false;
    refresh();
    if (availability_ != KeyboardAvailability::ready) return false;

    // Never create a missing attribute: only the driver can provide this ABI.
    const int fd = open(strength_path_.c_str(), O_WRONLY | O_CLOEXEC | O_TRUNC);
    if (fd < 0) return false;
    const std::string value = std::to_string(percent) + "\n";
    ssize_t written;
    do {
        written = write(fd, value.data(), value.size());
    } while (written < 0 && errno == EINTR);
    const int closed   = close(fd);
    const bool success = written == static_cast<ssize_t>(value.size()) && closed == 0;
    refresh();
    return success && availability_ != KeyboardAvailability::unavailable && strength_ == percent;
}

int KeyboardModel::strength() const noexcept
{
    return strength_;
}

KeyboardAvailability KeyboardModel::availability() const noexcept
{
    return availability_;
}

}  // namespace lilygo::settings
