#include "pages/ethernet/ethernet_view.hpp"

#include "components/settings_widgets.hpp"
#include "components/ui_helpers.hpp"

#include <cm0/typography.h>

namespace lilygo::settings {

EthernetView::EthernetView(EthernetViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *EthernetView::create(lv_obj_t *parent)
{
    destroy();
    root_ = components::create_page(parent, "settings_ethernet");
    if (!root_) return nullptr;
    lv_obj_set_style_pad_left(root_, 16, 0);
    lv_obj_set_style_pad_right(root_, 16, 0);
    lv_obj_set_style_pad_bottom(root_, 48, 0);
    lv_obj_set_style_pad_row(root_, 10, 0);

    auto constrain = [](lv_obj_t *object) {
        lv_obj_set_width(object, LV_PCT(100));
        lv_obj_set_style_max_width(object, 680, 0);
    };

    auto header = components::create_detail_header(root_, "Ethernet", "settings_ethernet_back");
    constrain(header.root);

    lv_obj_t *summary = components::create_surface(root_);
    constrain(summary);
    lv_obj_set_height(summary, 108);
    lv_obj_set_layout(summary, LV_LAYOUT_GRID);
    static const std::int32_t summary_columns[] = {64, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const std::int32_t summary_rows[]    = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(summary, summary_columns, summary_rows);
    lv_obj_set_style_pad_all(summary, 18, 0);
    lv_obj_set_style_pad_column(summary, 14, 0);
    lv_obj_t *icon = components::create_label(summary, LV_SYMBOL_DRIVE, 28, components::theme::accent);
    lv_obj_set_grid_cell(icon, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 2);
    status_ = components::create_label(summary, "Connected", 28, components::theme::text, "settings_ethernet_status");
    lv_obj_set_grid_cell(status_, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_END, 0, 1);
    interface_ =
        components::create_label(summary, "eth0", 16, components::theme::muted_text, "settings_ethernet_interface");
    lv_obj_set_grid_cell(interface_, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_START, 1, 1);

    constrain(components::create_section_label(root_, "NETWORK DETAILS"));
    lv_obj_t *network_group = components::create_group(root_);
    constrain(network_group);
    ipv4_ = components::create_value_row(network_group, "IP Address", "192.168.1.42", components::theme::muted_text,
                                         true, "settings_ethernet_ipv4_row")
                .value;
    (void)components::create_value_row(network_group, "Subnet Mask", "--", components::theme::muted_text, true,
                                       "settings_ethernet_subnet_row");
    (void)components::create_value_row(network_group, "Router", "--", components::theme::muted_text, true,
                                       "settings_ethernet_router_row");
    (void)components::create_value_row(network_group, "DNS", "--", components::theme::muted_text, false,
                                       "settings_ethernet_dns_row");

    constrain(components::create_section_label(root_, "LINK DETAILS"));
    lv_obj_t *link_group = components::create_group(root_);
    constrain(link_group);
    mac_ = components::create_value_row(link_group, "MAC Address", "02:00:00:00:00:20", components::theme::muted_text,
                                        true, "settings_ethernet_mac_row")
               .value;
    speed_ = components::create_value_row(link_group, "Speed", "1000 Mbps", components::theme::muted_text, true,
                                          "settings_ethernet_speed_row")
                 .value;
    duplex_ = components::create_value_row(link_group, "Duplex", "Full", components::theme::muted_text, true,
                                           "settings_ethernet_duplex_row")
                  .value;
    mtu_ = components::create_value_row(link_group, "MTU", "1500", components::theme::muted_text, false,
                                        "settings_ethernet_mtu_row")
               .value;

    lv_subject_add_observer_obj(view_model_.revision_subject(), presentation_changed, root_, this);
    render();
    return root_;
}

void EthernetView::destroy() noexcept
{
    if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = status_ = interface_ = ipv4_ = mac_ = speed_ = duplex_ = mtu_ = nullptr;
}

void EthernetView::presentation_changed(lv_observer_t *observer, lv_subject_t *)
{
    static_cast<EthernetView *>(lv_observer_get_user_data(observer))->render();
}

void EthernetView::render() noexcept
{
    const auto &state = view_model_.presentation();
    components::set_text(status_, state.status.c_str());
    components::set_text(interface_, state.interface_name.c_str());
    components::set_text(ipv4_, state.ipv4.c_str());
    components::set_text(mac_, state.mac.c_str());
    components::set_text(speed_, state.speed.c_str());
    components::set_text(duplex_, state.duplex.c_str());
    components::set_text(mtu_, state.mtu.c_str());
}

}  // namespace lilygo::settings
