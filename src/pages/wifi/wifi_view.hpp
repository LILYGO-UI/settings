#ifndef LILYGO_UI_SETTINGS_PAGES_WIFI_WIFI_VIEW_HPP
#define LILYGO_UI_SETTINGS_PAGES_WIFI_WIFI_VIEW_HPP

#include "pages/wifi/wifi_view_model.hpp"

#include <lvgl.h>

#include <cstddef>
#include <array>
#include <limits>
#include <string>

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
    static void known_clicked(lv_event_t *event);
    static void known_info_clicked(lv_event_t *event);
    static void details_back_clicked(lv_event_t *event);
    static void details_tab_clicked(lv_event_t *event);
    static void details_forget_clicked(lv_event_t *event);
    static void manage_cancel_clicked(lv_event_t *event);
    static void manage_join_clicked(lv_event_t *event);
    static void manage_forget_clicked(lv_event_t *event);
    static void layout_changed(lv_event_t *event);
    static void password_cancel_clicked(lv_event_t *event);
    static void password_connect_clicked(lv_event_t *event);
    static void password_keyboard_ready(lv_event_t *event);
    static void password_keyboard_cancelled(lv_event_t *event);

    void render() noexcept;
    void rebuild_networks();
    void create_details();
    void render_details();
    void show_details();
    void select_details_tab(std::size_t index);
    void show_network_actions(const std::string &uuid, const std::string &ssid, bool confirm);
    void hide_network_actions();
    void resize_overlays();
    lv_obj_t *add_info_button(lv_obj_t *row, const char *name, std::size_t index, lv_event_cb_t callback);
    void show_password_dialog();
    void hide_password_dialog() noexcept;
    void connect_pending_network();
    void create_empty_row(lv_obj_t *group, const char *message);
    static void set_group_height(lv_obj_t *group, std::size_t rows) noexcept;

    WifiViewModel &view_model_;
    lv_obj_t *root_            = nullptr;
    lv_obj_t *switch_          = nullptr;
    lv_obj_t *status_          = nullptr;
    lv_obj_t *connected_group_ = nullptr;
    lv_obj_t *network_group_   = nullptr;
    lv_obj_t *known_group_     = nullptr;
    lv_obj_t *known_status_    = nullptr;
    lv_obj_t *details_         = nullptr;
    lv_obj_t *details_title_   = nullptr;
    lv_obj_t *details_status_  = nullptr;
    lv_obj_t *details_content_ = nullptr;
    lv_obj_t *details_forget_  = nullptr;
    std::array<lv_obj_t *, 3> detail_tabs_{};
    std::array<lv_obj_t *, 3> detail_panes_{};
    std::array<lv_obj_t *, 12> detail_values_{};
    lv_obj_t *manage_overlay_      = nullptr;
    lv_obj_t *manage_title_        = nullptr;
    lv_obj_t *manage_message_      = nullptr;
    lv_obj_t *manage_join_         = nullptr;
    lv_obj_t *manage_forget_       = nullptr;
    lv_obj_t *manage_forget_label_ = nullptr;
    std::string managed_uuid_;
    std::string managed_ssid_;
    bool confirming_forget_                = false;
    lv_obj_t *password_overlay_            = nullptr;
    lv_obj_t *password_title_              = nullptr;
    lv_obj_t *password_input_              = nullptr;
    lv_obj_t *password_cancel_             = nullptr;
    lv_obj_t *password_join_               = nullptr;
    lv_obj_t *keyboard_                    = nullptr;
    std::size_t rendered_generation_       = std::numeric_limits<std::size_t>::max();
    std::size_t rendered_known_generation_ = std::numeric_limits<std::size_t>::max();
    bool rendered_available_               = false;
    bool rendered_enabled_                 = false;
    bool updating_                         = false;
};

}  // namespace lilygo::settings

#endif
