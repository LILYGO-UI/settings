#include "pages/ethernet/ethernet_view.hpp"

#include "components/settings_widgets.hpp"
#include "components/ui_helpers.hpp"

#include <cm0/input.h>
#include <cm0/typography.h>

#include <string>

namespace lilygo::settings {
namespace {

void constrain_width(lv_obj_t *object)
{
    if (!object) return;
    lv_obj_set_width(object, LV_PCT(100));
    lv_obj_set_style_max_width(object, LV_PCT(100), 0);
}

lv_obj_t *create_action_button(lv_obj_t *parent, const char *name, const char *text)
{
    if (!parent) return nullptr;
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_name_static(button, name);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, 56);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(components::theme::primary_action.rgb), 0);
    lv_obj_set_style_bg_opa(button, components::theme::primary_action.opacity, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(components::theme::pressed_background.rgb),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(components::theme::container_background.rgb),
                              LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(button, components::theme::container_background.opacity,
                            LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_t *label = components::create_label(button, text, 20, components::theme::accent_content);
    if (label) {
        lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_center(label);
        lv_obj_set_style_text_color(label, lv_color_hex(components::theme::muted_text.rgb),
                                    LV_PART_MAIN | LV_STATE_DISABLED);
        lv_obj_set_style_text_opa(label, components::theme::muted_text.opacity,
                                  LV_PART_MAIN | LV_STATE_DISABLED);
    }
    return button;
}

}  // namespace

EthernetView::EthernetView(EthernetViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *EthernetView::create(lv_obj_t *parent)
{
    destroy();
    const lv_font_t *input_font    = lilygo_ui_font_get(20);
    const lv_font_t *keyboard_font = lilygo_ui_font_get(16);
    if (!input_font || !keyboard_font) return nullptr;

    root_ = components::create_page(parent, "settings_ethernet");
    if (!root_) return nullptr;
    lv_obj_set_style_pad_left(root_, 16, 0);
    lv_obj_set_style_pad_right(root_, 16, 0);
    lv_obj_set_style_pad_bottom(root_, 48, 0);
    lv_obj_set_style_pad_row(root_, 10, 0);

    auto header = components::create_detail_header(root_, "Ethernet", "settings_ethernet_back");
    constrain_width(header.root);

    lv_obj_t *summary = components::create_surface(root_);
    constrain_width(summary);
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

    constrain_width(components::create_section_label(root_, "NETWORK DETAILS"));
    lv_obj_t *network_group = components::create_group(root_);
    constrain_width(network_group);
    const auto ipv4_row = components::create_value_row(network_group, "IP Address", "--", components::theme::muted_text,
                                                        true, "settings_ethernet_ipv4_row");
    const auto subnet_row = components::create_value_row(network_group, "Subnet Mask", "--",
                                                          components::theme::muted_text, true,
                                                          "settings_ethernet_subnet_row");
    const auto gateway_row = components::create_value_row(network_group, "Gateway", "--", components::theme::muted_text,
                                                           true, "settings_ethernet_gateway_row");
    const auto dns_row = components::create_value_row(network_group, "DNS", "--", components::theme::muted_text, false,
                                                       "settings_ethernet_dns_row");
    ipv4_ = ipv4_row.value;
    subnet_ = subnet_row.value;
    gateway_ = gateway_row.value;
    dns_ = dns_row.value;

    constrain_width(components::create_section_label(root_, "IPV4 CONFIGURATION"));
    config_panel_ = components::create_surface(root_, "settings_ethernet_config_panel");
    constrain_width(config_panel_);
    lv_obj_set_style_pad_all(config_panel_, 16, 0);
    lv_obj_set_style_pad_row(config_panel_, 8, 0);

    lv_obj_t *mode_label = components::create_label(config_panel_, "IPv4 Mode", 16, components::theme::section_label,
                                                     "settings_ethernet_ipv4_mode_label");
    constrain_width(mode_label);
    config_mode_ = lv_dropdown_create(config_panel_);
    lv_obj_set_name_static(config_mode_, "settings_ethernet_ipv4_mode");
    lv_obj_set_width(config_mode_, LV_PCT(100));
    lv_obj_set_height(config_mode_, 52);
    lv_dropdown_set_options(config_mode_, "DHCP\nManual");
    lv_obj_set_style_text_font(config_mode_, input_font, LV_PART_MAIN);
    lv_obj_set_style_text_font(config_mode_, input_font, LV_PART_INDICATOR);
    lv_obj_set_style_text_font(config_mode_, input_font, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(config_mode_, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(config_mode_, 12, LV_PART_MAIN);
    lv_obj_set_style_radius(config_mode_, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(config_mode_, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(config_mode_, lv_color_hex(components::theme::border.rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_color(config_mode_, lv_color_hex(components::theme::container_background.rgb), LV_PART_MAIN);
    lv_obj_add_event_cb(config_mode_, ipv4_mode_changed, LV_EVENT_VALUE_CHANGED, this);

    config_ip_ = create_configuration_input(config_panel_, "IP Address", "settings_ethernet_ip_input");
    config_subnet_ = create_configuration_input(config_panel_, "Subnet Mask", "settings_ethernet_subnet_input");
    config_gateway_ = create_configuration_input(config_panel_, "Gateway", "settings_ethernet_gateway_input");
    config_dns_ = create_configuration_input(config_panel_, "DNS", "settings_ethernet_dns_input");
    for (lv_obj_t *input : {config_ip_, config_subnet_, config_gateway_, config_dns_}) {
        if (!input) continue;
        lv_obj_set_style_text_font(input, input_font, LV_PART_MAIN);
        lv_obj_add_event_cb(input, config_field_clicked, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(input, config_field_clicked, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(input, config_field_clicked, LV_EVENT_VALUE_CHANGED, this);
    }

    config_apply_ = create_action_button(config_panel_, "settings_ethernet_config_apply", "Save");
    lv_obj_add_event_cb(config_apply_, config_apply_clicked, LV_EVENT_CLICKED, this);
    config_status_ = components::create_label(config_panel_, "", 16, components::theme::section_label,
                                              "settings_ethernet_config_status");
    constrain_width(config_status_);
    lv_obj_set_flag(config_status_, LV_OBJ_FLAG_HIDDEN, true);

    configuration_status_label_ = components::create_label(root_, "", 16, components::theme::section_label,
                                                            "settings_ethernet_configuration_status");
    constrain_width(configuration_status_label_);
    lv_obj_set_flag(configuration_status_label_, LV_OBJ_FLAG_HIDDEN, true);

    constrain_width(components::create_section_label(root_, "LINK DETAILS"));
    lv_obj_t *link_group = components::create_group(root_);
    constrain_width(link_group);
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

    // Keep a software keyboard object for platforms that choose to display it. The
    // target board uses its hardware keyboard, so the inline keyboard remains hidden.
    config_keyboard_ = lv_keyboard_create(root_);
    lv_obj_set_name_static(config_keyboard_, "settings_ethernet_config_keyboard");
    lv_obj_set_width(config_keyboard_, LV_PCT(100));
    lv_obj_set_height(config_keyboard_, LV_PCT(38));
    lv_obj_set_style_text_font(config_keyboard_, keyboard_font, LV_PART_ITEMS);
    lv_keyboard_set_mode(config_keyboard_, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_add_flag(config_keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(config_keyboard_, config_keyboard_ready, LV_EVENT_READY, this);
    lv_obj_add_event_cb(config_keyboard_, config_keyboard_cancelled, LV_EVENT_CANCEL, this);
    cm0_input_set_focus_scope(root_);

    lv_subject_add_observer_obj(view_model_.revision_subject(), presentation_changed, root_, this);
    render();
    return root_;
}

lv_obj_t *EthernetView::create_configuration_input(lv_obj_t *parent, const char *label, const char *name)
{
    lv_obj_t *caption = components::create_label(parent, label, 16, components::theme::section_label);
    constrain_width(caption);
    lv_obj_t *input = lv_textarea_create(parent);
    lv_obj_set_name_static(input, name);
    lv_obj_set_width(input, LV_PCT(100));
    lv_obj_set_height(input, 52);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, 64);
    lv_textarea_set_placeholder_text(input, "Not set");
    lv_obj_set_style_radius(input, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_left(input, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(input, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(input, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(input, lv_color_hex(components::theme::border.rgb), LV_PART_MAIN);
    return input;
}

void EthernetView::destroy() noexcept
{
    if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = status_ = interface_ = ipv4_ = subnet_ = gateway_ = dns_ = mac_ = speed_ = duplex_ = mtu_ = nullptr;
    configuration_status_label_ = config_panel_ = config_status_ = config_mode_ = config_ip_ = config_subnet_ =
        config_gateway_ = config_dns_ = config_apply_ = config_keyboard_ = active_input_ = nullptr;
    updating_controls_ = false;
    inputs_initialized_ = false;
    inputs_dirty_ = false;
    mode_manual_ = true;
}

void EthernetView::presentation_changed(lv_observer_t *observer, lv_subject_t *)
{
    static_cast<EthernetView *>(lv_observer_get_user_data(observer))->render();
}

void EthernetView::ipv4_mode_changed(lv_event_t *event)
{
    auto *self = static_cast<EthernetView *>(lv_event_get_user_data(event));
    if (!self || self->updating_controls_) return;
    self->mode_manual_ = lv_dropdown_get_selected(lv_event_get_target_obj(event)) != 0;
    self->inputs_dirty_ = true;
    self->update_ipv4_field_states();
}

void EthernetView::config_field_clicked(lv_event_t *event)
{
    auto *self = static_cast<EthernetView *>(lv_event_get_user_data(event));
    if (!self) return;
    lv_obj_t *field = lv_event_get_target_obj(event);
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        self->inputs_dirty_ = true;
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_CLICKED && self->config_keyboard_) {
        lv_obj_clear_flag(self->config_keyboard_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view_recursive(self->config_keyboard_, LV_ANIM_OFF);
    }
    if (self->active_input_ != field) self->focus_configuration_field(field);
}

void EthernetView::config_apply_clicked(lv_event_t *event)
{
    auto *self = static_cast<EthernetView *>(lv_event_get_user_data(event));
    if (self) self->apply_configuration();
}

void EthernetView::config_keyboard_ready(lv_event_t *event)
{
    auto *self = static_cast<EthernetView *>(lv_event_get_user_data(event));
    if (self) self->apply_configuration();
}

void EthernetView::config_keyboard_cancelled(lv_event_t *event)
{
    auto *self = static_cast<EthernetView *>(lv_event_get_user_data(event));
    if (self) {
        self->active_input_ = nullptr;
        lv_obj_add_flag(self->config_keyboard_, LV_OBJ_FLAG_HIDDEN);
    }
}

void EthernetView::focus_configuration_field(lv_obj_t *field) noexcept
{
    if (!field || !config_keyboard_) return;
    active_input_ = field;
    lv_keyboard_set_textarea(config_keyboard_, field);
    if (lv_group_t *group = lv_obj_get_group(field); group && lv_group_get_focused(group) != field)
        lv_group_focus_obj(field);
}

void EthernetView::set_configuration_inputs_from_presentation()
{
    const auto &state = view_model_.presentation();
    mode_manual_ = !state.ipv4_dhcp;
    updating_controls_ = true;
    if (config_mode_) lv_dropdown_set_selected(config_mode_, mode_manual_ ? 1 : 0);
    updating_controls_ = false;
    const auto set_input = [](lv_obj_t *input, const std::string &value) {
        if (!input) return;
        lv_textarea_set_text(input, value == "--" ? "" : value.c_str());
    };
    set_input(config_ip_, state.ipv4);
    set_input(config_subnet_, state.subnet);
    set_input(config_gateway_, state.gateway);
    set_input(config_dns_, state.dns);
    inputs_initialized_ = true;
    inputs_dirty_ = false;
}

void EthernetView::apply_configuration()
{
    if (!config_ip_ || !config_subnet_ || !config_gateway_ || !config_dns_) return;
    bool started = false;
    if (mode_manual_) {
        started = view_model_.apply_configuration(lv_textarea_get_text(config_ip_), lv_textarea_get_text(config_subnet_),
                                                   lv_textarea_get_text(config_gateway_), lv_textarea_get_text(config_dns_));
    } else {
        started = view_model_.apply_dhcp_configuration(lv_textarea_get_text(config_dns_));
    }
    if (started) {
        inputs_dirty_ = false;
        active_input_ = nullptr;
        if (config_keyboard_) lv_obj_add_flag(config_keyboard_, LV_OBJ_FLAG_HIDDEN);
    }
    if (!started && config_status_) {
        const auto &state = view_model_.presentation();
        lv_label_set_text(config_status_, state.configuration_status.empty() ? "Could not save Ethernet settings."
                                                                               : state.configuration_status.c_str());
        lv_obj_clear_flag(config_status_, LV_OBJ_FLAG_HIDDEN);
    }
}

void EthernetView::update_ipv4_field_states() noexcept
{
    const auto &state = view_model_.presentation();
    const bool disabled = updating_controls_ || state.configuration_busy || !state.configurable || !mode_manual_;
    for (lv_obj_t *input : {config_ip_, config_subnet_, config_gateway_})
        if (input) lv_obj_set_state(input, LV_STATE_DISABLED, disabled);
    if (config_dns_) lv_obj_set_state(config_dns_, LV_STATE_DISABLED,
                                      updating_controls_ || state.configuration_busy || !state.configurable);
}

void EthernetView::update_configuration_controls() noexcept
{
    const auto &state = view_model_.presentation();
    updating_controls_ = true;
    const bool enabled = state.configurable && !state.configuration_busy;
    if (config_mode_) lv_obj_set_state(config_mode_, LV_STATE_DISABLED, !enabled);
    if (config_apply_) lv_obj_set_state(config_apply_, LV_STATE_DISABLED, !enabled);
    updating_controls_ = false;
    update_ipv4_field_states();
}

void EthernetView::render() noexcept
{
    const auto &state = view_model_.presentation();
    components::set_text(status_, state.status.c_str());
    components::set_text(interface_, state.interface_name.c_str());
    components::set_text(ipv4_, state.ipv4.c_str());
    components::set_text(subnet_, state.subnet.c_str());
    components::set_text(gateway_, state.gateway.c_str());
    components::set_text(dns_, state.dns.c_str());
    components::set_text(mac_, state.mac.c_str());
    components::set_text(speed_, state.speed.c_str());
    components::set_text(duplex_, state.duplex.c_str());
    components::set_text(mtu_, state.mtu.c_str());
    if (!inputs_initialized_ || (!state.configuration_busy &&
                                  (state.configuration_status == "Ethernet Settings Saved" || !inputs_dirty_)))
        set_configuration_inputs_from_presentation();
    update_configuration_controls();
    if (config_status_) {
        lv_label_set_text(config_status_, state.configuration_status.c_str());
        lv_obj_set_flag(config_status_, LV_OBJ_FLAG_HIDDEN, state.configuration_status.empty());
    }
    if (configuration_status_label_) {
        lv_label_set_text(configuration_status_label_, state.configuration_status.c_str());
        lv_obj_set_flag(configuration_status_label_, LV_OBJ_FLAG_HIDDEN, state.configuration_status.empty());
    }
    if (state.configuration_status == "Ethernet Settings Saved" && config_keyboard_) {
        lv_obj_add_flag(config_keyboard_, LV_OBJ_FLAG_HIDDEN);
        active_input_ = nullptr;
    }
}

}  // namespace lilygo::settings
