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
    void render() noexcept;

    EthernetViewModel &view_model_;
    lv_obj_t *root_      = nullptr;
    lv_obj_t *status_    = nullptr;
    lv_obj_t *interface_ = nullptr;
    lv_obj_t *ipv4_      = nullptr;
    lv_obj_t *mac_       = nullptr;
    lv_obj_t *speed_     = nullptr;
    lv_obj_t *duplex_    = nullptr;
    lv_obj_t *mtu_       = nullptr;
};

}  // namespace lilygo::settings

#endif
