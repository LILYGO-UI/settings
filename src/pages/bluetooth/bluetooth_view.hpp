#ifndef LILYGO_UI_SETTINGS_PAGES_BLUETOOTH_BLUETOOTH_VIEW_HPP
#define LILYGO_UI_SETTINGS_PAGES_BLUETOOTH_BLUETOOTH_VIEW_HPP

#include "pages/bluetooth/bluetooth_view_model.hpp"

#include <lvgl.h>

#include <cstddef>
#include <limits>

namespace lilygo::settings {

class BluetoothView {
public:
    explicit BluetoothView(BluetoothViewModel &view_model) noexcept;
    [[nodiscard]] lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;

private:
    static void presentation_changed(lv_observer_t *observer, lv_subject_t *subject);
    static void switch_changed(lv_event_t *event);
    static void device_clicked(lv_event_t *event);
    void render() noexcept;
    void rebuild_devices();
    void create_empty_row(const char *message);
    void set_group_height(std::size_t rows) noexcept;

    BluetoothViewModel &view_model_;
    lv_obj_t *root_                  = nullptr;
    lv_obj_t *switch_                = nullptr;
    lv_obj_t *status_                = nullptr;
    lv_obj_t *device_group_          = nullptr;
    std::size_t rendered_generation_ = std::numeric_limits<std::size_t>::max();
    bool rendered_available_         = false;
    bool rendered_enabled_           = false;
    bool updating_                   = false;
};

}  // namespace lilygo::settings

#endif
