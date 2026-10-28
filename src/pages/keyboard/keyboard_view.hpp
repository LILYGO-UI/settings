#ifndef LILYGO_UI_SETTINGS_PAGES_KEYBOARD_KEYBOARD_VIEW_HPP
#define LILYGO_UI_SETTINGS_PAGES_KEYBOARD_KEYBOARD_VIEW_HPP

#include "pages/keyboard/keyboard_view_model.hpp"

namespace lilygo::settings {

class KeyboardView {
public:
    explicit KeyboardView(KeyboardViewModel &view_model) noexcept;
    [[nodiscard]] lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;

private:
    static void strength_changed(lv_event_t *event);
    static void presentation_changed(lv_observer_t *observer, lv_subject_t *subject);
    void render() noexcept;

    KeyboardViewModel &view_model_;
    lv_obj_t *root_   = nullptr;
    lv_obj_t *slider_ = nullptr;
    lv_obj_t *value_  = nullptr;
    lv_obj_t *status_ = nullptr;
};

}  // namespace lilygo::settings

#endif
