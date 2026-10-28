#ifndef LILYGO_UI_SETTINGS_PAGES_KEYBOARD_KEYBOARD_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_KEYBOARD_KEYBOARD_MODEL_HPP

#include <string>

namespace lilygo::settings {

struct KeyboardEnvironment {
    std::string driver_root;
    static KeyboardEnvironment from_process_environment();
};

enum class KeyboardAvailability { unavailable, read_only, ready };

class KeyboardModel {
public:
    explicit KeyboardModel(KeyboardEnvironment environment = KeyboardEnvironment::from_process_environment());
    void refresh();
    [[nodiscard]] bool set_strength(int percent);
    [[nodiscard]] int strength() const noexcept;
    [[nodiscard]] KeyboardAvailability availability() const noexcept;

private:
    KeyboardEnvironment environment_;
    std::string strength_path_;
    int strength_                      = 0;
    KeyboardAvailability availability_ = KeyboardAvailability::unavailable;
};

}  // namespace lilygo::settings

#endif
