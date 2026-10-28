#ifndef LILYGO_UI_SETTINGS_PAGES_ETHERNET_ETHERNET_VIEW_HPP
#define LILYGO_UI_SETTINGS_PAGES_ETHERNET_ETHERNET_VIEW_HPP

#include "pages/ethernet/ethernet_view_model.hpp"

#include <lvgl.h>

namespace lilygo::settings {

class EthernetView {
public:
    explicit EthernetView(EthernetViewModel &view_model) noexcept;
    [[nodiscard]] lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;

private:
    static void presentation_changed(lv_observer_t *observer, lv_subject_t *subject);
    static void ipv4_mode_changed(lv_event_t *event);
    static void config_field_clicked(lv_event_t *event);
    static void config_apply_clicked(lv_event_t *event);
    static void config_keyboard_ready(lv_event_t *event);
    static void config_keyboard_cancelled(lv_event_t *event);

    void render() noexcept;
    void apply_configuration();
    void focus_configuration_field(lv_obj_t *field) noexcept;
    void set_configuration_inputs_from_presentation();
    void update_configuration_controls() noexcept;
    void update_ipv4_field_states() noexcept;
    lv_obj_t *create_configuration_input(lv_obj_t *parent, const char *label, const char *name);

    EthernetViewModel &view_model_;
    lv_obj_t *root_        = nullptr;
    lv_obj_t *status_      = nullptr;
    lv_obj_t *interface_   = nullptr;
    lv_obj_t *ipv4_        = nullptr;
    lv_obj_t *subnet_      = nullptr;
    lv_obj_t *gateway_     = nullptr;
    lv_obj_t *dns_         = nullptr;
    lv_obj_t *mac_         = nullptr;
    lv_obj_t *speed_       = nullptr;
    lv_obj_t *duplex_      = nullptr;
    lv_obj_t *mtu_         = nullptr;
    lv_obj_t *configuration_status_label_ = nullptr;
    lv_obj_t *config_panel_ = nullptr;
    lv_obj_t *config_status_ = nullptr;
    lv_obj_t *config_mode_ = nullptr;
    lv_obj_t *config_ip_ = nullptr;
    lv_obj_t *config_subnet_ = nullptr;
    lv_obj_t *config_gateway_ = nullptr;
    lv_obj_t *config_dns_ = nullptr;
    lv_obj_t *config_apply_ = nullptr;
    lv_obj_t *config_keyboard_ = nullptr;
    lv_obj_t *active_input_ = nullptr;
    bool updating_controls_ = false;
    bool inputs_initialized_ = false;
    bool inputs_dirty_ = false;
    bool mode_manual_ = true;
};

}  // namespace lilygo::settings

#endif
