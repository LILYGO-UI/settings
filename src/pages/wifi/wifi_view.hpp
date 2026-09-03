#ifndef LILYGO_UI_SETTINGS_PAGES_WIFI_WIFI_VIEW_HPP
#define LILYGO_UI_SETTINGS_PAGES_WIFI_WIFI_VIEW_HPP

#include "pages/wifi/wifi_view_model.hpp"

#include <lvgl.h>

#include <cstddef>
#include <limits>

namespace lilygo::settings {

class WifiView {
public:
    explicit WifiView(WifiViewModel &view_model) noexcept;
    [[nodiscard]] lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;

private:
    static void presentation_changed(lv_observer_t *observer, lv_subject_t *subject);
    static void switch_changed(lv_event_t *event);
    static void network_clicked(lv_event_t *event);
    static void password_cancel_clicked(lv_event_t *event);
    static void password_connect_clicked(lv_event_t *event);
    static void password_input_ready(lv_event_t *event);

    void render() noexcept;
    void rebuild_networks();
    void show_password_dialog();
    void hide_password_dialog() noexcept;
    void connect_pending_network();
    void create_empty_row(lv_obj_t *group, const char *message);
    static void set_group_height(lv_obj_t *group, std::size_t rows) noexcept;

    WifiViewModel &view_model_;
    lv_obj_t *root_                  = nullptr;
    lv_obj_t *switch_                = nullptr;
    lv_obj_t *status_                = nullptr;
    lv_obj_t *connected_group_       = nullptr;
    lv_obj_t *network_group_         = nullptr;
    lv_obj_t *password_overlay_      = nullptr;
    lv_obj_t *password_title_        = nullptr;
    lv_obj_t *password_input_        = nullptr;
    lv_obj_t *password_cancel_       = nullptr;
    lv_obj_t *password_join_         = nullptr;
    lv_obj_t *keyboard_              = nullptr;
    std::size_t rendered_generation_ = std::numeric_limits<std::size_t>::max();
    bool rendered_available_         = false;
    bool rendered_enabled_           = false;
    bool updating_                   = false;
};

}  // namespace lilygo::settings

#endif
