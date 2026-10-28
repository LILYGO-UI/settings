#ifndef LILYGO_UI_SETTINGS_PAGES_KEYBOARD_KEYBOARD_VIEW_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_KEYBOARD_KEYBOARD_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "pages/keyboard/keyboard_model.hpp"

#include <string>

namespace lilygo::settings {

struct KeyboardPresentation {
    int strength = 0;
    bool enabled = false;
    std::string value;
    std::string status;
};

class KeyboardViewModel {
public:
    explicit KeyboardViewModel(KeyboardEnvironment environment = KeyboardEnvironment::from_process_environment());
    void refresh();
    void set_strength(int percent);
    [[nodiscard]] const KeyboardPresentation &presentation() const noexcept;
    [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
    void publish();
    KeyboardModel model_;
    KeyboardPresentation presentation_;
    bool apply_failed_ = false;
    components::RevisionSubject revision_;
};

}  // namespace lilygo::settings

#endif
