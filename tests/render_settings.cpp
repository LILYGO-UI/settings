#include "app.hpp"
#include "app_identity.h"
#include "settings_fixture.hpp"

#include <cm0/status_bar.h>
#include <cm0/typography.h>

#include <lvgl.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using lilygo::settings::test_support::SettingsFixture;

constexpr int kPartialRows                   = 80;
constexpr int kMaximumDimension              = 1232;
constexpr std::uint32_t kPageBackground      = 0xf2f2f7;
constexpr std::uint32_t kContainerBackground = 0xffffff;
constexpr std::uint32_t kContainerTitle      = 0x1a1a1a;
constexpr std::uint32_t kText                = 0x000000;
constexpr std::uint32_t kBorder              = 0xd8dde3;
constexpr std::uint32_t kPrimaryAction       = 0x20262d;
constexpr char kLongestWifiSsid[]            = "12345678901234567890123456789012";
constexpr char kLongCellularOperator[]       = "中国移动 · LILYGO International Test Network";
static_assert(sizeof(kLongestWifiSsid) - 1 == 32);

std::vector<lv_color32_t> framebuffer;
int display_width  = 568;
int display_height = 1232;

void flush_display(lv_display_t *display, const lv_area_t *area, std::uint8_t *pixels)
{
    const int width    = area->x2 - area->x1 + 1;
    const auto *source = reinterpret_cast<const lv_color32_t *>(pixels);
    for (int y = area->y1; y <= area->y2; ++y) {
        const auto source_offset = static_cast<std::size_t>(y - area->y1) * static_cast<std::size_t>(width);
        const auto destination_offset =
            static_cast<std::size_t>(y) * kMaximumDimension + static_cast<std::size_t>(area->x1);
        std::copy_n(source + source_offset, width, framebuffer.begin() + destination_offset);
    }
    lv_display_flush_ready(display);
}

void write_ppm(const char *path)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output << "P6\n" << display_width << ' ' << display_height << "\n255\n";
    for (int y = 0; y < display_height; ++y) {
        for (int x = 0; x < display_width; ++x) {
            const auto &pixel =
                framebuffer[static_cast<std::size_t>(y) * kMaximumDimension + static_cast<std::size_t>(x)];
            output.put(static_cast<char>(pixel.red));
            output.put(static_cast<char>(pixel.green));
            output.put(static_cast<char>(pixel.blue));
        }
    }
    assert(output.good());
}

lv_obj_t *find_label(lv_obj_t *root, const std::string &text, bool visible_only = false)
{
    if (!root) return nullptr;
    if (lv_obj_check_type(root, &lv_label_class) && (!visible_only || lv_obj_is_visible(root)) &&
        text == lv_label_get_text(root))
        return root;
    const auto child_count = lv_obj_get_child_count(root);
    for (std::uint32_t index = 0; index < child_count; ++index) {
        if (auto *match = find_label(lv_obj_get_child(root, index), text, visible_only)) return match;
    }
    return nullptr;
}

lv_obj_t *find_named(lv_obj_t *root, const char *name)
{
    return root ? lv_obj_find_by_name(root, name) : nullptr;
}

bool is_descendant_of(lv_obj_t *object, lv_obj_t *ancestor)
{
    for (auto *current = object; current; current = lv_obj_get_parent(current)) {
        if (current == ancestor) return true;
    }
    return false;
}

std::size_t timer_count()
{
    std::size_t count = 0;
    for (lv_timer_t *timer = lv_timer_get_next(nullptr); timer; timer = lv_timer_get_next(timer)) ++count;
    return count;
}

void pump_timers(int iterations = 80)
{
    for (int iteration = 0; iteration < iterations; ++iteration) {
        lv_tick_inc(20);
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

bool wait_for_label(lv_obj_t *root, const std::string &text)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (find_label(root, text)) return true;
        lv_tick_inc(20);
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return find_label(root, text) != nullptr;
}

bool wait_for_label_in(lv_obj_t *container, const std::string &text)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (find_label(container, text, true)) return true;
        lv_tick_inc(20);
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return find_label(container, text, true) != nullptr;
}

lv_obj_t *wait_for_named(lv_obj_t *container, const char *name)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto *object = find_named(container, name)) return object;
        lv_tick_inc(20);
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return find_named(container, name);
}

bool wait_for_control_enabled(lv_obj_t *control)
{
    assert(control);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!lv_obj_has_state(control, LV_STATE_DISABLED)) return true;
        lv_tick_inc(20);
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool wait_for_command(const SettingsFixture &fixture, const std::string &text)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (fixture.command_log().find(text) != std::string::npos) return true;
        lv_tick_inc(20);
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return fixture.command_log().find(text) != std::string::npos;
}

void click_named(lv_obj_t *root, const char *name)
{
    lv_obj_t *object = find_named(root, name);
    assert(object && lv_obj_is_visible(object));
    lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr);
    lv_obj_update_layout(root);
}

const char *settings_navigation_row(const char *home_name, const char *landscape_name)
{
    return display_width > display_height ? landscape_name : home_name;
}

void click_label(lv_obj_t *root, const std::string &text)
{
    lv_obj_t *label = find_label(root, text, true);
    if (!label) {
        label = find_label(root, text);
        if (label) {
            lv_obj_scroll_to_view(label, LV_ANIM_OFF);
            lv_obj_update_layout(root);
        }
    }
    assert(label && lv_obj_is_visible(label));
    lv_obj_t *row = lv_obj_get_parent(label);
    assert(row && is_descendant_of(row, root));
    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    lv_obj_update_layout(root);
}

void click_label_in(lv_obj_t *container, const std::string &text)
{
    lv_obj_t *label = find_label(container, text, true);
    if (!label) {
        label = find_label(container, text);
        if (label) {
            lv_obj_scroll_to_view_recursive(label, LV_ANIM_OFF);
            lv_obj_update_layout(container);
        }
    }
    assert(label && lv_obj_is_visible(label));
    lv_obj_t *row = lv_obj_get_parent(label);
    assert(row && is_descendant_of(row, container));
    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    lv_obj_update_layout(container);
}

void assert_current_page(lv_obj_t *root, const char *current_name)
{
    constexpr const char *page_names[] = {
        "settings_home",     "settings_wifi",    "settings_bluetooth", "settings_ethernet", "settings_cellular", "settings_battery",
        "settings_keyboard", "settings_general", "settings_about",     "settings_sharing",
    };
    for (const char *name : page_names) {
        lv_obj_t *page = find_named(root, name);
        assert(page && is_descendant_of(page, root));
        assert(lv_obj_has_flag(page, LV_OBJ_FLAG_HIDDEN) != (std::strcmp(name, current_name) == 0));
    }
}

void assert_menu_row_content_centered(lv_obj_t *root, const char *row_name, const char *title, bool image_icon = true)
{
    lv_obj_t *row = find_named(root, row_name);
    assert(row && lv_obj_get_child_count(row) == 5);

    lv_obj_t *label = lv_obj_get_child(row, 1);
    assert(label && lv_obj_check_type(label, &lv_label_class));

    lv_obj_t *icon = lv_obj_get_child(row, 0);
    assert(icon && lv_obj_check_type(icon, image_icon ? &lv_image_class : &lv_label_class));
    if (image_icon) assert(lv_obj_get_width(icon) == 42 && lv_obj_get_height(icon) == 42);
    else assert(lv_obj_get_width(icon) > 0 && lv_obj_get_height(icon) > 0);

    lv_area_t row_coordinates;
    lv_obj_get_coords(row, &row_coordinates);
    const auto row_center = (row_coordinates.y1 + row_coordinates.y2 - 1) / 2;
    for (std::uint32_t index = 0; index < 4; ++index) {
        lv_obj_t *content = lv_obj_get_child(row, index);
        if (!lv_obj_has_flag(content, LV_OBJ_FLAG_HIDDEN)) {
            lv_area_t coordinates;
            lv_obj_get_coords(content, &coordinates);
            const auto content_center = (coordinates.y1 + coordinates.y2) / 2;
            if (std::abs(content_center - row_center) > 1)
                std::fprintf(stderr, "%s child %u row=[%d,%d] child=[%d,%d] delta=%d\n", title, index,
                             row_coordinates.y1, row_coordinates.y2, coordinates.y1, coordinates.y2,
                             std::abs(content_center - row_center));
            assert(std::abs(content_center - row_center) <= 1);
            if (display_width < 400) {
                assert(coordinates.x1 >= row_coordinates.x1 && coordinates.x2 <= row_coordinates.x2);
                assert(coordinates.y1 >= row_coordinates.y1 && coordinates.y2 <= row_coordinates.y2);
                if (lv_obj_check_type(content, &lv_label_class)) {
                    const auto self_height    = lv_obj_get_self_height(content);
                    const auto content_height = lv_obj_get_content_height(content);
                    if (self_height > content_height)
                        std::fprintf(stderr, "%s child %u label='%s' self=%dx%d content=%dx%d\n", title, index,
                                     lv_label_get_text(content), lv_obj_get_self_width(content), self_height,
                                     lv_obj_get_content_width(content), content_height);
                    assert(self_height <= content_height);
                }
            }
        }
    }
}

void assert_compact_home_avoids_system_area(lv_obj_t *root)
{
    lv_obj_t *row    = find_named(root, "settings_home_general_row");
    lv_obj_t *footer = find_named(root, "settings_home_footer");
    assert(row && footer);
    lv_obj_update_layout(root);

    // The footer follows the Keyboard group, so compact displays may need to
    // scroll before it can be inspected. Keep the assertion about the system
    // area while exercising the same path a user uses to reach the footer.
    lv_obj_scroll_to_view_recursive(footer, LV_ANIM_OFF);
    lv_obj_update_layout(root);

    lv_area_t coordinates;
    lv_obj_get_coords(row, &coordinates);
    assert(coordinates.y2 < display_height - 38);
    lv_obj_get_coords(footer, &coordinates);
    assert(coordinates.y2 < display_height - 38);
}

void assert_row_reachable(lv_obj_t *root, const char *page_name, const char *last_row_text)
{
    lv_obj_t *page  = find_named(root, page_name);
    lv_obj_t *label = find_label(page, last_row_text);
    assert(page && label);
    lv_obj_scroll_to_y(page, lv_obj_get_scroll_y(page) + lv_obj_get_scroll_bottom(page), LV_ANIM_OFF);
    lv_obj_update_layout(root);
    lv_area_t coordinates;
    lv_obj_get_coords(lv_obj_get_parent(label), &coordinates);
    assert(coordinates.y2 < display_height - 38);
}

lv_obj_t *create_runtime_root()
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xf7f7f8), 0);

    lv_obj_t *system_layer = lv_layer_top();
    lv_obj_t *status       = cm0_status_bar_create(system_layer);
    assert(status);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *content = lv_obj_create(screen);
    lv_obj_set_name_static(content, "appkit_runtime_root");
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_top(content, CM0_STATUS_BAR_HEIGHT, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_bg_color(content, lv_color_hex(kPageBackground), 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *home_indicator = cm0_home_indicator_create(system_layer);
    assert(home_indicator);
    lv_obj_set_size(home_indicator, LV_PCT(100), 38);
    lv_obj_align(home_indicator, LV_ALIGN_BOTTOM_MID, 0, -3);
    return content;
}

lv_obj_t *open_application(const cm0_app_descriptor_t *descriptor, cm0_app_context_t &context, lv_obj_t *runtime_screen,
                           std::size_t baseline_timers)
{
    descriptor->open(&context);
    lv_obj_update_layout(context.root);
    assert(lv_screen_active() == runtime_screen);

    lv_obj_t *surface = find_named(context.root, "settings_surface");
    assert(surface);
    assert(lv_obj_get_parent(surface) == context.root);
    assert(lv_obj_get_screen(surface) == runtime_screen);
    assert(timer_count() == baseline_timers + 2);
    assert_current_page(context.root, "settings_home");
    return surface;
}

void close_application(const cm0_app_descriptor_t *descriptor, cm0_app_context_t &context, lv_obj_t *runtime_screen,
                       lv_obj_t *surface, std::size_t baseline_timers, bool require_fast_close)
{
    const auto started = std::chrono::steady_clock::now();
    descriptor->close();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (require_fast_close) assert(elapsed < std::chrono::seconds(2));
    assert(lv_screen_active() == runtime_screen);
    assert(!lv_obj_is_valid(surface));
    assert(find_named(context.root, "settings_surface") == nullptr);
    assert(timer_count() == baseline_timers);
    pump_timers(2);
    assert(find_named(context.root, "settings_surface") == nullptr);
}

void assert_initial_visual_contract(lv_obj_t *root)
{
    lv_obj_t *home           = find_named(root, "settings_home");
    lv_obj_t *settings_title = find_label(root, "Settings", true);
    lv_obj_t *wifi_row       = find_named(root, "settings_home_wifi_row");
    lv_obj_t *wifi_title     = wifi_row ? lv_obj_get_child(wifi_row, 1) : nullptr;
    lv_obj_t *wifi_status    = find_named(root, "settings_wifi_status");
    lv_obj_t *ethernet_state = find_named(root, "settings_ethernet_status");
    lv_obj_t *battery_level  = find_named(root, "settings_battery_percent");
    lv_obj_t *ipv4_row       = find_named(root, "settings_ethernet_ipv4_row");
    lv_obj_t *password_input = find_named(root, "settings_password_input");
    lv_obj_t *keyboard       = find_named(root, "settings_password_keyboard");
    assert(home && settings_title && wifi_row && wifi_title && wifi_status && ethernet_state && battery_level &&
           ipv4_row && password_input && keyboard);
    assert(lv_color_eq(lv_obj_get_style_bg_color(home, LV_PART_MAIN), lv_color_hex(kPageBackground)));
    assert(lv_obj_get_style_bg_opa(home, LV_PART_MAIN) == LV_OPA_COVER);
    assert(lv_obj_get_style_bg_grad_dir(home, LV_PART_MAIN) == LV_GRAD_DIR_NONE);
    assert(lv_color_eq(lv_obj_get_style_text_color(home, LV_PART_MAIN), lv_color_hex(kText)));
    assert(lv_color_eq(lv_obj_get_style_text_color(settings_title, LV_PART_MAIN), lv_color_hex(kContainerTitle)));
    assert(lv_obj_get_style_text_font(settings_title, LV_PART_MAIN) == lilygo_ui_font_get(38));
    assert(lv_obj_get_style_text_font(wifi_title, LV_PART_MAIN) == lilygo_ui_font_get(22));
    assert(lv_obj_get_style_text_font(wifi_status, LV_PART_MAIN) == lilygo_ui_font_get(16));
    assert(lv_obj_get_style_text_font(ethernet_state, LV_PART_MAIN) == lilygo_ui_font_get(28));
    assert(lv_obj_get_style_text_font(battery_level, LV_PART_MAIN) == lilygo_ui_font_get(48));
    assert(lv_obj_get_style_text_font(lv_obj_get_child(ipv4_row, 0), LV_PART_MAIN) == lilygo_ui_font_get(20));
    assert(lv_obj_get_style_text_font(lv_obj_get_child(ipv4_row, 1), LV_PART_MAIN) == lilygo_ui_font_get(18));
    assert(lv_obj_get_style_text_font(password_input, LV_PART_MAIN) == lilygo_ui_font_get(22));
    assert(lv_obj_get_style_text_font(keyboard, LV_PART_ITEMS) == lilygo_ui_font_get(16));
    assert(lv_color_eq(lv_obj_get_style_text_color(wifi_title, LV_PART_MAIN), lv_color_hex(kContainerTitle)));

    auto *connectivity_group = find_named(root, "settings_home_connectivity_group");
    auto *general_group      = find_named(root, "settings_home_general_group");
    auto *keyboard_group     = find_named(root, "settings_home_keyboard_group");
    assert(connectivity_group && general_group && keyboard_group);
    assert(lv_obj_get_child_count(connectivity_group) == 5);
    for (const char *name : {"settings_home_wifi_row", "settings_home_bluetooth_row", "settings_home_ethernet_row", "settings_home_cellular_row",
                             "settings_home_battery_row"})
        assert(lv_obj_get_parent(find_named(root, name)) == connectivity_group);
    assert(lv_obj_get_parent(find_named(root, "settings_home_general_row")) == general_group);
    assert(lv_obj_get_parent(find_named(root, "settings_home_keyboard_row")) == keyboard_group);

    for (const char *name :
         {"settings_home_wifi_row", "settings_home_bluetooth_row", "settings_home_ethernet_row", "settings_home_cellular_row",
          "settings_home_battery_row", "settings_home_general_row", "settings_home_keyboard_row", "settings_wifi_back",
          "settings_bluetooth_back", "settings_ethernet_back", "settings_cellular_back", "settings_cellular_switch", "settings_battery_back", "settings_keyboard_back",
          "settings_general_back", "settings_about_back", "settings_general_sharing_row", "settings_sharing_back",
          "settings_sharing_remote_login_switch", "settings_sharing_serial_switch", "settings_sharing_reboot_now",
          "settings_sharing_reboot_later"}) {
        lv_obj_t *control = find_named(root, name);
        assert(control && lv_obj_is_group_def(control));
    }

    for (const char *name :
         {"settings_wifi_back", "settings_bluetooth_back", "settings_ethernet_back", "settings_cellular_back", "settings_battery_back",
          "settings_keyboard_back", "settings_general_back", "settings_about_back", "settings_sharing_back"}) {
        auto *back = find_named(root, name);
        assert(back);
        assert(lv_obj_get_style_radius(back, LV_PART_MAIN) == 22);
        assert(lv_color_eq(lv_obj_get_style_bg_color(back, LV_PART_MAIN), lv_color_hex(kContainerBackground)));
    }

    lv_obj_t *settings_menu = lv_obj_get_parent(lv_obj_get_parent(wifi_title));
    assert(settings_menu && is_descendant_of(settings_menu, root));
    assert(lv_color_eq(lv_obj_get_style_bg_color(settings_menu, LV_PART_MAIN), lv_color_hex(kContainerBackground)));
    assert(lv_obj_get_style_bg_opa(settings_menu, LV_PART_MAIN) == LV_OPA_COVER);

    lv_obj_t *system_layer = lv_layer_top();
    lv_obj_update_layout(system_layer);
    lv_obj_t *status_bar = find_named(system_layer, "status_bar");
    assert(status_bar && lv_obj_get_height(status_bar) == CM0_STATUS_BAR_HEIGHT);
    lv_obj_t *wifi         = find_named(status_bar, "cm0_status_bar_wifi");
    lv_obj_t *battery      = find_named(status_bar, "cm0_status_bar_battery");
    lv_obj_t *battery_bar  = find_named(status_bar, "cm0_status_bar_battery_level");
    lv_obj_t *battery_text = find_named(status_bar, "cm0_status_bar_battery_text");
    assert(wifi && lv_obj_get_width(wifi) == 21);
    assert(battery && lv_obj_get_width(battery) == 45);
    assert(battery_bar && battery_text);
    assert(std::string(lv_label_get_text(battery_text)) == "82");
    assert(find_named(system_layer, "cm0_home_indicator_bar"));
}

void enter_password_page(lv_obj_t *root, const char *ssid = "Workshop")
{
    click_named(root, settings_navigation_row("settings_home_wifi_row", "settings_landscape_wifi_row"));
    assert_current_page(root, "settings_wifi");
    if (std::strcmp(ssid, kLongestWifiSsid) == 0) {
        assert(wait_for_label(root, "Workshop"));
        lv_obj_t *group = find_named(root, "settings_wifi_network_group");
        assert(group && lv_obj_get_child_count(group) == 3);
        lv_obj_t *row = lv_obj_get_child(group, static_cast<std::int32_t>(lv_obj_get_child_count(group) - 1U));
        assert(row && is_descendant_of(row, root));
        lv_obj_scroll_to_view_recursive(row, LV_ANIM_OFF);
        lv_obj_update_layout(root);
        if (!lv_obj_is_visible(row)) {
            auto dump_geometry = [](const char *name, lv_obj_t *object) {
                lv_area_t area;
                lv_obj_get_coords(object, &area);
                std::fprintf(stderr, "%s coords=[%d,%d,%d,%d] height=%d scroll_y=%d top=%d bottom=%d hidden=%d\n", name,
                             area.x1, area.y1, area.x2, area.y2, lv_obj_get_height(object), lv_obj_get_scroll_y(object),
                             lv_obj_get_scroll_top(object), lv_obj_get_scroll_bottom(object),
                             lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN));
            };
            dump_geometry("long-ssid-row", row);
            dump_geometry("wifi-network-group", group);
            dump_geometry("wifi-page", find_named(root, "settings_wifi"));
        }
        assert(lv_obj_is_visible(row));
        lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *title = find_named(root, "settings_password_title");
        assert(title);
        // LVGL's DOT mode temporarily overwrites the displayed suffix; a refresh restores the semantic text.
        lv_label_set_text(title, nullptr);
        assert(std::string(lv_label_get_text(title)) == std::string("Join ") + ssid);
        lv_obj_update_layout(root);
    } else {
        assert(wait_for_label(root, ssid));
        click_label(root, ssid);
    }
    lv_obj_t *overlay = find_named(root, "settings_password_overlay");
    assert(overlay && lv_obj_is_visible(overlay));
    lv_obj_t *join = find_named(root, "settings_password_join");
    assert(join);
    assert(lv_color_eq(lv_obj_get_style_bg_color(join, LV_PART_MAIN), lv_color_hex(kPrimaryAction)));
}

void assert_compact_password_title_fits(lv_obj_t *root)
{
    const std::string expected = std::string("Join ") + kLongestWifiSsid;
    lv_obj_t *title            = find_named(root, "settings_password_title");
    lv_obj_t *input            = find_named(root, "settings_password_input");
    assert(title && input);
    lv_obj_update_layout(root);

    const std::string rendered = lv_label_get_text(title);
    assert(lv_label_get_long_mode(title) == LV_LABEL_LONG_DOT);
    assert(rendered.rfind("Join ", 0) == 0);
    assert(rendered.size() < expected.size());
    assert(rendered.size() >= 3 && rendered.compare(rendered.size() - 3, 3, "...") == 0);
    assert(lv_obj_get_self_width(title) <= lv_obj_get_content_width(title));
    assert(lv_obj_get_self_height(title) <= lv_obj_get_content_height(title));

    lv_obj_t *dialog = lv_obj_get_parent(title);
    assert(dialog && lv_obj_get_parent(input) == dialog);
    lv_area_t dialog_area;
    lv_area_t title_area;
    lv_area_t input_area;
    lv_obj_get_content_coords(dialog, &dialog_area);
    lv_obj_get_coords(title, &title_area);
    lv_obj_get_coords(input, &input_area);
    assert(title_area.x1 >= dialog_area.x1 && title_area.x2 <= dialog_area.x2);
    assert(title_area.y1 >= dialog_area.y1 && title_area.y2 <= dialog_area.y2);
    assert(title_area.y2 < input_area.y1);
}

void resize_with_input_preserved(lv_display_t *display, lv_obj_t *root, lv_obj_t *runtime_screen)
{
    enter_password_page(root);
    lv_obj_t *input = find_named(root, "settings_password_input");
    assert(input && lv_obj_check_type(input, &lv_textarea_class));
    lv_textarea_set_text(input, "state-survives-resize");
    lv_obj_t *surface   = find_named(root, "settings_surface");
    lv_obj_t *wifi_page = find_named(root, "settings_wifi");
    assert(lv_obj_has_state(input, LV_STATE_FOCUSED));

    display_width  = 568;
    display_height = 568;
    lv_display_set_resolution(display, display_width, display_height);
    lv_obj_update_layout(root);
    assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
    assert(lv_obj_has_flag(wifi_page, LV_OBJ_FLAG_SCROLLABLE));
    assert(lv_obj_get_scroll_bottom(wifi_page) > 0);
    lv_obj_scroll_to_y(wifi_page, 24, LV_ANIM_OFF);
    lv_obj_update_layout(root);
    const auto scroll_before_resize = lv_obj_get_scroll_y(wifi_page);
    assert(scroll_before_resize > 0);

    display_width  = 1232;
    display_height = 568;
    std::fill(framebuffer.begin(), framebuffer.end(), lv_color32_t{});
    lv_display_set_resolution(display, display_width, display_height);
    lv_obj_update_layout(root);

    assert(lv_screen_active() == runtime_screen);
    assert(find_named(root, "settings_surface") == surface);
    assert(find_named(root, "settings_wifi") == wifi_page);
    assert_current_page(root, "settings_wifi");
    assert(std::string(lv_textarea_get_text(input)) == "state-survives-resize");
    assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
    assert(lv_obj_get_scroll_y(wifi_page) == scroll_before_resize);
    lv_obj_t *wifi_switch = find_named(root, "settings_wifi_switch");
    assert(wifi_switch && lv_obj_has_state(wifi_switch, LV_STATE_CHECKED));
}

void enter_wifi_details(lv_obj_t *root, const SettingsFixture &fixture)
{
    click_named(root, settings_navigation_row("settings_home_wifi_row", "settings_landscape_wifi_row"));
    auto *connected = find_named(root, "settings_wifi_connected_group");
    assert(connected && wait_for_label_in(connected, "CM0 Lab"));
    // Selecting the current connection now inspects it without disconnecting it.
    click_label_in(connected, "CM0 Lab");
    auto *details = find_named(root, "settings_wifi_details");
    assert(details && lv_obj_is_visible(details));
    assert(fixture.command_log().find("device disconnect") == std::string::npos);
    click_named(root, "settings_wifi_details_back");
    assert(lv_obj_has_flag(details, LV_OBJ_FLAG_HIDDEN));
    click_named(root, "settings_wifi_connected_details");
    assert(lv_obj_is_visible(details));
}

void assert_wifi_details_layout(lv_obj_t *root, const char *active_pane)
{
    auto *page    = find_named(root, "settings_wifi");
    auto *details = find_named(root, "settings_wifi_details");
    auto *content = find_named(root, "settings_wifi_details_content");
    assert(page && details && content && lv_obj_is_visible(details));
    lv_obj_update_layout(root);
    lv_area_t page_area, details_area, content_area;
    lv_obj_get_coords(page, &page_area);
    lv_obj_get_coords(details, &details_area);
    lv_obj_get_coords(content, &content_area);
    assert(details_area.x1 >= page_area.x1 && details_area.x2 <= page_area.x2);
    assert(details_area.y1 >= page_area.y1 && details_area.y2 <= page_area.y2);
    assert(content_area.x1 >= details_area.x1 && content_area.x2 <= details_area.x2);
    assert(content_area.y1 >= details_area.y1 && content_area.y2 <= details_area.y2);
    for (const auto *name : {"settings_wifi_details_back", "settings_wifi_tab_tcpip", "settings_wifi_tab_dns",
                             "settings_wifi_tab_hardware"}) {
        auto *button = find_named(details, name);
        assert(button && lv_obj_is_visible(button) && lv_obj_is_group_def(button));
        lv_area_t area;
        lv_obj_get_coords(button, &area);
        assert(area.x1 >= details_area.x1 && area.x2 <= details_area.x2);
        assert(area.y1 >= details_area.y1 && area.y2 < content_area.y1);
        assert(lv_obj_get_height(button) >= 44);
    }
    for (const auto *name : {"settings_wifi_tcpip", "settings_wifi_dns", "settings_wifi_hardware"}) {
        auto *pane = find_named(details, name);
        assert(pane && lv_obj_has_flag(pane, LV_OBJ_FLAG_HIDDEN) != (std::strcmp(name, active_pane) == 0));
    }
}

void assert_named_label(lv_obj_t *root, const char *name, const char *expected)
{
    auto *value = find_named(root, name);
    assert(value && lv_obj_check_type(value, &lv_label_class));
    assert(wait_for_label(value, expected));
    lv_obj_update_layout(root);
    lv_area_t value_area, row_area;
    lv_obj_get_coords(value, &value_area);
    lv_obj_get_content_coords(lv_obj_get_parent(value), &row_area);
    assert(value_area.x1 >= row_area.x1 && value_area.x2 <= row_area.x2);
    assert(value_area.y1 >= row_area.y1 && value_area.y2 <= row_area.y2);
    assert(lv_obj_get_self_height(value) <= lv_obj_get_content_height(value));
}

void assert_cellular_layout(lv_obj_t *root)
{
    auto *page    = find_named(root, "settings_cellular");
    auto *control = find_named(page, "settings_cellular_switch");
    auto *row     = find_named(page, "settings_cellular_enabled_row");
    assert(page && control && row);
    lv_obj_update_layout(root);
    lv_area_t page_area, row_area, control_area, title_area;
    lv_obj_get_content_coords(page, &page_area);
    lv_obj_get_content_coords(row, &row_area);
    lv_obj_get_coords(control, &control_area);
    auto *title = find_label(row, "Cellular");
    assert(title);
    lv_obj_get_coords(title, &title_area);
    assert(title_area.x2 < control_area.x1);
    assert(control_area.x1 >= row_area.x1 && control_area.x2 <= row_area.x2);
    assert(lv_obj_get_height(row) >= 44);

    for (const auto *name : {"settings_cellular_sim_status", "settings_cellular_iccid", "settings_cellular_imsi",
                             "settings_cellular_operator", "settings_cellular_technology", "settings_cellular_signal",
                             "settings_cellular_interface", "settings_cellular_received", "settings_cellular_transmitted",
                             "settings_cellular_total"}) {
        auto *value = find_named(page, name);
        assert(value && lv_obj_check_type(value, &lv_label_class));
        auto *value_row = lv_obj_get_parent(value);
        auto *label = lv_obj_get_child(value_row, 0);
        assert(label && lv_obj_check_type(label, &lv_label_class));
        lv_area_t value_area, label_area;
        lv_obj_get_coords(value, &value_area);
        lv_obj_get_coords(label, &label_area);
        lv_obj_get_content_coords(value_row, &row_area);
        assert(row_area.x1 >= page_area.x1 && row_area.x2 <= page_area.x2);
        assert(label_area.x2 < value_area.x1);
        assert(value_area.x1 >= row_area.x1 && value_area.x2 <= row_area.x2);
        assert(value_area.y1 >= row_area.y1 && value_area.y2 <= row_area.y2);
        assert(lv_obj_get_self_height(value) <= lv_obj_get_content_height(value));
        assert(lv_obj_get_self_height(label) <= lv_obj_get_content_height(label));
    }
}

void assert_wifi_manage_layout(lv_obj_t *root)
{
    auto *page    = find_named(root, "settings_wifi");
    auto *overlay = find_named(root, "settings_wifi_manage_overlay");
    assert(page && overlay && lv_obj_is_visible(overlay));
    lv_obj_update_layout(root);
    lv_area_t page_area, overlay_area;
    lv_obj_get_coords(page, &page_area);
    lv_obj_get_coords(overlay, &overlay_area);
    assert(overlay_area.x1 == page_area.x1 && overlay_area.x2 == page_area.x2);
    assert(overlay_area.y1 == page_area.y1 && overlay_area.y2 == page_area.y2);
    for (const auto *name :
         {"settings_wifi_known_join", "settings_wifi_forget_confirm", "settings_wifi_manage_cancel"}) {
        auto *button = find_named(overlay, name);
        assert(button);
        if (lv_obj_has_flag(button, LV_OBJ_FLAG_HIDDEN)) continue;
        assert(lv_obj_is_visible(button) && lv_obj_get_height(button) >= 44);
        lv_area_t area;
        lv_obj_get_coords(button, &area);
        assert(area.x1 >= overlay_area.x1 && area.x2 <= overlay_area.x2);
        assert(area.y1 >= overlay_area.y1 && area.y2 <= overlay_area.y2);
    }
}

void resize_wifi_details(lv_display_t *display, lv_obj_t *root)
{
    click_named(root, "settings_wifi_tab_dns");
    auto *details  = find_named(root, "settings_wifi_details");
    auto *dns      = find_named(root, "settings_wifi_dns");
    auto *selected = find_named(root, "settings_wifi_tab_dns");
    lv_obj_add_state(selected, LV_STATE_FOCUSED);
    for (const auto width : {1232, 320, 568}) {
        display_width  = width;
        display_height = width == 568 ? 1232 : 568;
        lv_display_set_resolution(display, display_width, display_height);
        lv_obj_update_layout(root);
        assert(find_named(root, "settings_wifi_details") == details);
        assert(find_named(root, "settings_wifi_dns") == dns);
        assert(lv_obj_has_state(selected, LV_STATE_FOCUSED));
        assert_wifi_details_layout(root, "settings_wifi_dns");
    }
}

void run_scenario(const std::string &scenario, lv_display_t *display, lv_obj_t *root, lv_obj_t *runtime_screen,
                  const SettingsFixture &fixture)
{
    if (scenario == "home" || scenario == "close-active-command") return;
    if (scenario == "wifi") {
        click_named(root, settings_navigation_row("settings_home_wifi_row", "settings_landscape_wifi_row"));
        assert_current_page(root, "settings_wifi");
    } else if (scenario.rfind("wifi-details-", 0) == 0 || scenario == "wifi-forget") {
        enter_wifi_details(root, fixture);
        if (scenario == "wifi-details-dns")
            click_named(root, "settings_wifi_tab_dns");
        else if (scenario == "wifi-details-hardware")
            click_named(root, "settings_wifi_tab_hardware");
        else if (scenario == "wifi-details-resize")
            resize_wifi_details(display, root);
        else if (scenario == "wifi-forget") {
            auto *forget = find_named(root, "settings_wifi_details_forget");
            assert(forget && wait_for_control_enabled(forget));
            lv_obj_scroll_to_view_recursive(forget, LV_ANIM_OFF);
            click_named(root, "settings_wifi_details_forget");
            auto *confirmation = find_named(root, "settings_wifi_manage_overlay");
            assert(confirmation && lv_obj_is_visible(confirmation));
            assert_wifi_manage_layout(root);
            assert(fixture.command_log().find("connection delete") == std::string::npos);
            click_named(root, "settings_wifi_manage_cancel");
            assert(lv_obj_has_flag(confirmation, LV_OBJ_FLAG_HIDDEN));
            assert(fixture.command_log().find("connection delete") == std::string::npos);
            click_named(root, "settings_wifi_details_forget");
            click_named(root, "settings_wifi_forget_confirm");
            assert(wait_for_command(fixture, "connection delete uuid 11111111-1111-4111-8111-111111111111"));
        }
    } else if (scenario == "wifi-known-connect") {
        click_named(root, settings_navigation_row("settings_home_wifi_row", "settings_landscape_wifi_row"));
        auto *known = find_named(root, "settings_wifi_known_group");
        assert(known);
        assert(!find_label(find_named(root, "settings_wifi_network_group"), "Home Network"));
        auto *info = wait_for_named(known, "settings_wifi_known_info_1");
        assert(info);
        auto *title = lv_obj_get_child(lv_obj_get_parent(info), 1);
        assert(title && lv_obj_check_type(title, &lv_label_class));
        // DOT mode changes the rendered label buffer on compact screens.
        lv_label_set_text(title, nullptr);
        assert(std::string(lv_label_get_text(title)) == "Home Network");
        lv_obj_scroll_to_view_recursive(info, LV_ANIM_OFF);
        click_named(root, "settings_wifi_known_info_1");
        auto *actions = find_named(root, "settings_wifi_manage_overlay");
        if (actions && !lv_obj_is_visible(actions)) {
            lv_area_t area;
            lv_obj_get_coords(actions, &area);
            std::fprintf(stderr, "known-network overlay coords=[%d,%d,%d,%d], hidden=%d; page scroll=%d\n",
                         area.x1, area.y1, area.x2, area.y2, lv_obj_has_flag(actions, LV_OBJ_FLAG_HIDDEN),
                         lv_obj_get_scroll_y(find_named(root, "settings_wifi")));
        }
        assert(actions && lv_obj_is_visible(actions));
        assert_wifi_manage_layout(root);
        assert(fixture.command_log().find("connection up uuid ") == std::string::npos);
        assert(fixture.command_log().find("connection delete uuid ") == std::string::npos);
        click_named(root, "settings_wifi_manage_cancel");
        assert(lv_obj_has_flag(actions, LV_OBJ_FLAG_HIDDEN));
        // An out-of-range saved network remains available independently of the scan list.
        auto *row = lv_obj_get_parent(find_named(known, "settings_wifi_known_info_1"));
        assert(row);
        lv_obj_scroll_to_view_recursive(row, LV_ANIM_OFF);
        lv_obj_update_layout(root);
        assert(lv_obj_is_visible(row));
        lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
        assert(wait_for_command(fixture, "connection up uuid 22222222-2222-4222-8222-222222222222"));
        auto *password = find_named(root, "settings_password_overlay");
        assert(password && lv_obj_has_flag(password, LV_OBJ_FLAG_HIDDEN));
        assert(fixture.command_log().find("device wifi connect Home Network") == std::string::npos);
    } else if (scenario == "wifi-password") {
        enter_password_page(root, display_width == 320 ? kLongestWifiSsid : "Workshop");
    } else if (scenario == "wifi-connect") {
        click_named(root, settings_navigation_row("settings_home_wifi_row", "settings_landscape_wifi_row"));
        assert(wait_for_label(root, "Guest"));
        click_label(root, "Guest");
        assert(wait_for_command(fixture, "device wifi connect Guest"));
    } else if (scenario == "wifi-secured-connect") {
        enter_password_page(root);
        lv_obj_t *input = find_named(root, "settings_password_input");
        assert(input);
        lv_textarea_set_text(input, "test-password");
        click_named(root, "settings_password_join");
        assert(wait_for_command(fixture, "--ask --wait 15 device wifi connect Workshop"));
        assert(fixture.command_log().find("test-password") == std::string::npos);
    } else if (scenario == "wifi-keyboard-connect") {
        enter_password_page(root);
        lv_obj_t *input    = find_named(root, "settings_password_input");
        lv_obj_t *keyboard = find_named(root, "settings_password_keyboard");
        lv_obj_t *overlay  = find_named(root, "settings_password_overlay");
        assert(input && keyboard && overlay && lv_obj_is_visible(overlay));
        lv_textarea_set_text(input, "test-password");
        lv_obj_send_event(keyboard, LV_EVENT_READY, nullptr);
        assert(wait_for_command(fixture, "--ask --wait 15 device wifi connect Workshop"));
        assert(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));
        assert(fixture.command_log().find("test-password") == std::string::npos);
    } else if (scenario == "bluetooth" || scenario == "bluetooth-connect") {
        assert(wait_for_label(root, "CM0 Lab"));
        click_named(root, settings_navigation_row("settings_home_bluetooth_row", "settings_landscape_bluetooth_row"));
        assert_current_page(root, "settings_bluetooth");
        if (scenario == "bluetooth-connect") {
            auto *group = find_named(root, "settings_bluetooth_device_group");
            assert(group && wait_for_label_in(group, "Keyboard"));
            click_label_in(group, "Keyboard");
            assert(wait_for_command(fixture, "connect AA:BB:CC:DD:EE:01"));
        }
    } else if (scenario.rfind("cellular", 0) == 0) {
        const auto *navigation = settings_navigation_row("settings_home_cellular_row", "settings_landscape_cellular_row");
        click_named(root, navigation);
        assert_current_page(root, "settings_cellular");
        auto *page    = find_named(root, "settings_cellular");
        auto *control = find_named(page, "settings_cellular_switch");
        assert(control);
        if (scenario == "cellular-unavailable") {
            assert(wait_for_label(page, "Cellular Modem Unavailable"));
            assert(fixture.command_log().find("AT\n") == std::string::npos);
            return;
        }
        assert(wait_for_control_enabled(control));
        assert(lv_obj_has_state(control, LV_STATE_CHECKED));
        if (scenario == "cellular-no-sim") {
            assert(wait_for_label(page, "No SIM Detected"));
            assert(fixture.command_log().find("AT+CIMI\n") == std::string::npos);
            return;
        }
        assert(wait_for_label(page, "89860012345678901234"));
        click_named(root, "settings_cellular_back");
        assert_current_page(root, "settings_home");
        click_named(root, navigation);
        assert_current_page(root, "settings_cellular");
        assert(find_named(root, "settings_cellular_switch") == control);
        assert(wait_for_control_enabled(control));

        if (scenario == "cellular-toggle" || scenario == "cellular-denied") {
            if (scenario == "cellular-denied") fixture.write("cellular-denied", "");
            auto change_enabled = [&](bool enabled) {
                lv_obj_set_state(control, LV_STATE_CHECKED, enabled);
                lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, nullptr);
                assert(lv_obj_has_state(control, LV_STATE_DISABLED));
                assert(wait_for_control_enabled(control));
            };
            const auto command_start = fixture.command_log().size();
            change_enabled(false);
            assert(wait_for_command(fixture, "AT+CFUN=20"));
            const auto log = fixture.command_log().substr(command_start);
            // An AT command acknowledgement does not confirm the actual radio state.
            assert(log.find("AT+CFUN?\n", log.find("AT+CFUN=20")) != std::string::npos);
            if (scenario == "cellular-denied") {
                assert(lv_obj_has_state(control, LV_STATE_CHECKED));
                assert(wait_for_label(page, "Modem Rejected Cellular Change"));
            } else {
                assert(!lv_obj_has_state(control, LV_STATE_CHECKED));
                assert(wait_for_label(page, "Cellular: Off"));
                change_enabled(true);
                assert(lv_obj_has_state(control, LV_STATE_CHECKED));
                assert(wait_for_command(fixture, "AT+CFUN=21"));
                fixture.write("cellular-noop", "");
                change_enabled(false);
                assert(lv_obj_has_state(control, LV_STATE_CHECKED));
                assert(wait_for_label(page, "Cellular Change Was Not Applied"));
                std::filesystem::remove(fixture.path("cellular-noop"));
                change_enabled(false);
                assert(!lv_obj_has_state(control, LV_STATE_CHECKED));
                change_enabled(true);
                assert(lv_obj_has_state(control, LV_STATE_CHECKED));
            }
        } else if (scenario == "cellular-resize") {
            auto *iccid = find_named(page, "settings_cellular_iccid");
            auto *group = lv_group_create();
            lv_group_add_obj(group, control);
            lv_group_focus_obj(control);
            display_width  = 320;
            display_height = 568;
            lv_display_set_resolution(display, display_width, display_height);
            lv_obj_update_layout(root);
            lv_obj_scroll_to_y(page, 80, LV_ANIM_OFF);
            lv_obj_update_layout(root);
            const auto scroll_before = lv_obj_get_scroll_y(page);
            assert(scroll_before > 0);
            for (const auto width : {1232, 320, 568}) {
                display_width  = width;
                display_height = width == 568 ? 1232 : 568;
                lv_display_set_resolution(display, display_width, display_height);
                lv_obj_update_layout(root);
                assert_current_page(root, "settings_cellular");
                assert(find_named(root, "settings_cellular") == page);
                assert(find_named(root, "settings_cellular_switch") == control);
                assert(find_named(root, "settings_cellular_iccid") == iccid);
                assert(lv_group_get_focused(group) == control);
                assert(lv_obj_has_state(control, LV_STATE_CHECKED));
                assert(lv_obj_has_state(control, LV_STATE_FOCUSED));
                const auto scroll_after = lv_obj_get_scroll_y(page);
                if (display_height == 568) {
                    assert(scroll_after == scroll_before);
                } else {
                    // A taller viewport can fit the full page, legitimately
                    // clamping the retained offset to the new scroll range.
                    const auto maximum_scroll = std::max<std::int32_t>(0, scroll_after + lv_obj_get_scroll_bottom(page));
                    assert(scroll_after == std::min(scroll_before, maximum_scroll));
                }
                assert_cellular_layout(root);
                assert_named_label(root, "settings_cellular_operator", kLongCellularOperator);
            }
            lv_group_delete(group);
            lv_obj_scroll_to_y(page, 0, LV_ANIM_OFF);
        }
    } else if (scenario == "ethernet" || scenario == "ethernet-config") {
        click_named(root, settings_navigation_row("settings_home_ethernet_row", "settings_landscape_ethernet_row"));
        assert_current_page(root, "settings_ethernet");
        if (scenario == "ethernet-config") {
            lv_group_t *input_group = lv_group_create();
            assert(input_group);
            for (const char *name :
                 {"settings_ethernet_ipv4_mode", "settings_ethernet_ip_input", "settings_ethernet_subnet_input",
                  "settings_ethernet_gateway_input", "settings_ethernet_dns_input", "settings_ethernet_config_apply"})
                lv_group_add_obj(input_group, find_named(root, name));
            auto *mode = find_named(root, "settings_ethernet_ipv4_mode");
            assert(mode && wait_for_control_enabled(mode));
            lv_dropdown_set_selected(mode, 0);
            lv_obj_send_event(mode, LV_EVENT_VALUE_CHANGED, nullptr);
            assert(lv_obj_has_state(find_named(root, "settings_ethernet_ip_input"), LV_STATE_DISABLED));
            assert(lv_obj_has_state(find_named(root, "settings_ethernet_subnet_input"), LV_STATE_DISABLED));
            assert(lv_obj_has_state(find_named(root, "settings_ethernet_gateway_input"), LV_STATE_DISABLED));
            assert(!lv_obj_has_state(find_named(root, "settings_ethernet_dns_input"), LV_STATE_DISABLED));
            lv_dropdown_set_selected(mode, 1);
            lv_obj_send_event(mode, LV_EVENT_VALUE_CHANGED, nullptr);
            lv_textarea_set_text(find_named(root, "settings_ethernet_ip_input"), "192.168.8.50");
            lv_textarea_set_text(find_named(root, "settings_ethernet_subnet_input"), "255.255.255.0");
            lv_textarea_set_text(find_named(root, "settings_ethernet_gateway_input"), "192.168.8.1");
            lv_textarea_set_text(find_named(root, "settings_ethernet_dns_input"), "1.1.1.1,8.8.8.8");
            click_named(root, "settings_ethernet_config_apply");
            assert(wait_for_command(fixture, "connection modify Wired connection 1"));
            assert(wait_for_command(fixture, "connection up Wired connection 1"));
            assert(wait_for_label(root, "Ethernet Settings Saved"));
            lv_group_delete(input_group);
        }
    } else if (scenario.rfind("battery", 0) == 0) {
        click_named(root, settings_navigation_row("settings_home_battery_row", "settings_landscape_battery_row"));
        assert_current_page(root, "settings_battery");
        auto *design = find_named(root, "settings_battery_design_capacity_input");
        auto *full = find_named(root, "settings_battery_full_capacity_input");
        assert(design && full);
        if (scenario == "battery-unsupported") {
            assert(lv_obj_has_state(design, LV_STATE_DISABLED));
            lv_obj_scroll_to_view_recursive(find_named(root, "settings_battery_capacity_message"), LV_ANIM_OFF);
        } else if (scenario == "battery-keyboard") {
            lv_obj_scroll_to_view_recursive(full, LV_ANIM_OFF);
            click_named(root, "settings_battery_full_capacity_input");
            auto *keyboard = find_named(root, "settings_battery_capacity_keyboard");
            assert(keyboard && lv_obj_is_visible(keyboard));
            assert(lv_keyboard_get_textarea(keyboard) == full);
            lv_textarea_set_text(full, "4300");
            lv_obj_send_event(keyboard, LV_EVENT_READY, nullptr);
            assert(!lv_obj_is_visible(keyboard));
            assert(std::string(lv_textarea_get_text(full)) == "4300");
            click_named(root, "settings_battery_full_capacity_input");
            assert(lv_obj_is_visible(keyboard));
        } else if (scenario == "battery-controls") {
            auto *slider = find_named(root, "settings_battery_input_current_slider");
            assert(slider && !lv_obj_has_state(slider, LV_STATE_DISABLED));
            lv_obj_scroll_to_view_recursive(slider, LV_ANIM_OFF);
            lv_slider_set_value(slider, 10, LV_ANIM_OFF);
            lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, nullptr);
            click_named(root, "settings_battery_input_current_apply");
            std::ifstream input(fixture.path("power/bq25890-charger/input_current_limit"));
            int value = 0;
            input >> value;
            assert(value == 600000);
            lv_obj_scroll_to_view_recursive(find_named(root, "settings_battery_charging_message"), LV_ANIM_OFF);
        } else if (scenario != "battery") {
            assert(!lv_obj_has_state(design, LV_STATE_DISABLED));
            lv_textarea_set_text(design, "5100");
            lv_textarea_set_text(full, "4300");
            if (scenario == "battery-resize") {
                lv_obj_add_state(full, LV_STATE_FOCUSED);
                display_width = 1232;
                display_height = 568;
                lv_display_set_resolution(display, display_width, display_height);
                lv_obj_update_layout(root);
                assert(find_named(root, "settings_battery_full_capacity_input") == full);
                assert(lv_obj_has_state(full, LV_STATE_FOCUSED));
                assert(std::string(lv_textarea_get_text(design)) == "5100");
                assert(std::string(lv_textarea_get_text(full)) == "4300");
            }
            lv_obj_scroll_to_view_recursive(find_named(root, "settings_battery_capacity_apply"), LV_ANIM_OFF);
            click_named(root, "settings_battery_capacity_apply");
            assert(lv_obj_is_visible(find_named(root, "settings_battery_capacity_confirmation")));
            assert(lv_obj_has_state(design, LV_STATE_DISABLED));
            lv_obj_scroll_to_view_recursive(find_named(root, "settings_battery_capacity_confirm"), LV_ANIM_OFF);
            if (scenario == "battery-save") {
                click_named(root, "settings_battery_capacity_confirm");
                std::ifstream input(fixture.path("power/BAT0/calibration_capacity"));
                int design_value = 0, full_value = 0;
                input >> design_value >> full_value;
                assert(design_value == 5100 && full_value == 4300);
                assert(!lv_obj_is_visible(find_named(root, "settings_battery_capacity_confirmation")));
                lv_obj_scroll_to_view_recursive(find_named(root, "settings_battery_capacity_message"), LV_ANIM_OFF);
            }
        }
    } else if (scenario == "keyboard") {
        const auto *navigation = settings_navigation_row("settings_home_keyboard_row", "settings_landscape_keyboard_row");
        auto *row = find_named(root, navigation);
        assert(row);
        lv_obj_scroll_to_view_recursive(row, LV_ANIM_OFF);
        lv_obj_update_layout(root);
        lv_area_t row_area;
        lv_obj_get_coords(row, &row_area);
        assert(lv_obj_is_visible(row));
        assert(row_area.y1 >= CM0_STATUS_BAR_HEIGHT && row_area.y2 < display_height - 38);
        click_named(root, navigation);
        assert_current_page(root, "settings_keyboard");
    } else if (scenario == "general") {
        click_named(root, settings_navigation_row("settings_home_general_row", "settings_landscape_general_row"));
        assert_current_page(root, "settings_general");
        click_named(root, "settings_general_about_row");
        assert_current_page(root, "settings_about");
        click_named(root, "settings_about_back");
        assert_current_page(root, "settings_general");
    } else if (scenario.rfind("serial", 0) == 0) {
        click_named(root, settings_navigation_row("settings_home_general_row", "settings_landscape_general_row"));
        click_named(root, "settings_general_sharing_row");
        assert(wait_for_label(root, "Serial Console: Off"));
        auto *control = find_named(root, "settings_sharing_serial_switch");
        assert(control && wait_for_control_enabled(control));
        lv_obj_scroll_to_view_recursive(control, LV_ANIM_OFF);
        lv_obj_add_state(control, LV_STATE_CHECKED);
        lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, nullptr);
        auto *overlay = find_named(root, "settings_sharing_reboot_overlay");
        assert(overlay && lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));
        assert(wait_for_label_in(overlay, "Restart Required"));
        assert(lv_obj_has_state(control, LV_STATE_CHECKED));
        assert(fixture.command_log().find("reboot") == std::string::npos);
        if (scenario == "serial-resize") {
            for (auto width : {1232, 320, 568}) {
                display_width  = width;
                display_height = width == 568 ? 1232 : 568;
                lv_display_set_resolution(display, display_width, display_height);
                lv_obj_update_layout(root);
                assert(find_named(root, "settings_sharing_reboot_overlay") == overlay);
                assert(lv_obj_is_visible(overlay));
                assert(lv_obj_has_state(control, LV_STATE_CHECKED));
            }
        } else if (scenario == "serial-later") {
            click_named(root, "settings_sharing_reboot_later");
            assert(!lv_obj_is_visible(overlay));
            assert(lv_obj_has_state(control, LV_STATE_CHECKED));
            assert(fixture.command_log().find("reboot") == std::string::npos);
            lv_obj_scroll_to_y(find_named(root, "settings_sharing"), 0, LV_ANIM_OFF);
            click_named(root, "settings_sharing_back");
            click_named(root, "settings_general_sharing_row");
            pump_timers(20);
            assert(!lv_obj_is_visible(overlay) && lv_obj_has_state(control, LV_STATE_CHECKED));
            assert(find_label(root, "Restart Required to Apply Serial Console"));
        } else if (scenario == "serial-reboot-failure") {
            fixture.write("reboot-denied", "");
            click_named(root, "settings_sharing_reboot_now");
            assert(wait_for_label_in(overlay, "Restart Failed. Try Again or Restart Later."));
            assert(lv_obj_is_visible(overlay));
        } else if (scenario == "serial-now") {
            click_named(root, "settings_sharing_reboot_now");
            assert(wait_for_label(root, "Restarting..."));
            assert(wait_for_command(fixture, "--no-ask-password reboot"));
        }
    } else if (scenario.rfind("sharing", 0) == 0) {
        click_named(root, settings_navigation_row("settings_home_general_row", "settings_landscape_general_row"));
        click_named(root, "settings_general_sharing_row");
        assert_current_page(root, "settings_sharing");
        assert(wait_for_label(root, "Remote Login: Off"));
        click_named(root, "settings_sharing_back");
        assert_current_page(root, "settings_general");
        click_named(root, "settings_general_sharing_row");
        pump_timers(20);
        auto *control = find_named(root, "settings_sharing_remote_login_switch");
        assert(control && wait_for_control_enabled(control));
        if (scenario == "sharing-focus") {
            auto *serial = find_named(root, "settings_sharing_serial_switch");
            assert(wait_for_control_enabled(serial));
            auto *group = lv_group_create();
            lv_group_add_obj(group, find_named(root, "settings_sharing_back"));
            lv_group_add_obj(group, control);
            lv_group_add_obj(group, serial);
            // Navigate using the same LVGL group operations as a physical keypad.
            for (auto *focused : {control, serial}) {
                lv_group_focus_next(group);
                assert(lv_group_get_focused(group) == focused);
                const auto log_start = fixture.command_log().size();
                for (int tick = 0; tick < 600; ++tick) {
                    lv_tick_inc(20);  // Cross at least two five-second refreshes.
                    lv_timer_handler();
                    assert(lv_group_get_focused(group) == focused);
                    assert(lv_obj_has_state(focused, LV_STATE_FOCUSED));
                    // AppKit removes disabled widgets from keyboard navigation.
                    assert(!lv_obj_has_state(control, LV_STATE_DISABLED));
                    assert(!lv_obj_has_state(serial, LV_STATE_DISABLED));
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                const auto log  = fixture.command_log().substr(log_start);
                const auto ssh  = log.find("--property=Id,LoadState,ActiveState,UnitFileState");
                const auto uart = log.find("nonint get_serial_cons");
                assert(ssh != std::string::npos && uart != std::string::npos);
                assert(log.find("--property=Id,LoadState,ActiveState,UnitFileState", ssh + 1) != std::string::npos);
                assert(log.find("nonint get_serial_cons", uart + 1) != std::string::npos);
            }
            lv_group_delete(group);
        } else if (scenario != "sharing") {
            lv_obj_add_state(control, LV_STATE_CHECKED);
            lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, nullptr);
            assert(lv_obj_has_state(control, LV_STATE_DISABLED));
            assert(wait_for_label(root, "Remote Login: On"));
            assert(lv_obj_has_state(control, LV_STATE_CHECKED));
            assert(wait_for_command(fixture, "enable --now ssh.service"));
        }
        if (scenario == "sharing-toggle") {
            lv_obj_remove_state(control, LV_STATE_CHECKED);
            lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, nullptr);
            assert(wait_for_label(root, "Remote Login: Off"));
            assert(!lv_obj_has_state(control, LV_STATE_CHECKED));
            assert(wait_for_command(fixture, "disable --now ssh.service"));
        } else if (scenario == "sharing-denied") {
            fixture.write("ssh-sudo-denied", "");
            lv_obj_remove_state(control, LV_STATE_CHECKED);
            lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, nullptr);
            assert(wait_for_label(root, "Administrator Permission Is Required to Change Remote Login."));
            assert(lv_obj_has_state(control, LV_STATE_CHECKED));
            assert(!lv_obj_has_state(control, LV_STATE_DISABLED));
        } else if (scenario == "sharing-resize") {
            lv_obj_add_state(control, LV_STATE_FOCUSED);
            for (const auto width : {1232, 320, 568}) {
                display_width  = width;
                display_height = width == 568 ? 1232 : 568;
                lv_display_set_resolution(display, display_width, display_height);
                lv_obj_update_layout(root);
                assert_current_page(root, "settings_sharing");
                assert(find_named(root, "settings_sharing_remote_login_switch") == control);
                assert(lv_obj_has_state(control, LV_STATE_CHECKED));
                assert(lv_obj_has_state(control, LV_STATE_FOCUSED));
            }
        }
    } else if (scenario == "about") {
        click_named(root, settings_navigation_row("settings_home_general_row", "settings_landscape_general_row"));
        click_named(root, "settings_general_about_row");
        assert_current_page(root, "settings_about");
    } else if (scenario == "resize-state" || scenario == "lifecycle" || scenario == "reopen-resize") {
        resize_with_input_preserved(display, root, runtime_screen);
    } else {
        assert(false && "unknown settings render scenario");
    }
}

void assert_scenario(const std::string &scenario, lv_obj_t *root)
{
    if (scenario == "home") {
        assert_current_page(root, "settings_home");
        assert(wait_for_label(root, display_width < 400 ? "Settings" : "CM0 Lab"));
        assert_menu_row_content_centered(root, "settings_home_wifi_row", "Wi-Fi");
        assert_menu_row_content_centered(root, "settings_home_bluetooth_row", "Bluetooth");
        assert_menu_row_content_centered(root, "settings_home_ethernet_row", "Ethernet");
        assert_menu_row_content_centered(root, "settings_home_cellular_row", "Cellular", false);
        assert_menu_row_content_centered(root, "settings_home_battery_row", "Battery");
        assert_menu_row_content_centered(root, "settings_home_general_row", "General");
        if (display_width < 400 && display_height < 720) assert_compact_home_avoids_system_area(root);
    } else if (scenario == "close-active-command") {
        assert_current_page(root, "settings_home");
        assert(find_label(root, "Settings", true));
    } else if (scenario == "wifi") {
        assert(wait_for_label(root, "CM0 Lab"));
        assert(wait_for_label(root, "Workshop"));
        assert(wait_for_named(find_named(root, "settings_wifi_known_group"), "settings_wifi_known_info_1"));
        lv_obj_t *group = find_named(root, "settings_wifi_network_group");
        assert(group && lv_obj_get_height(group) >= 166);
        lv_obj_t *workshop = find_label(group, "Workshop");
        assert(workshop);
        lv_obj_t *row     = lv_obj_get_parent(workshop);
        lv_obj_t *divider = lv_obj_get_child(row, 5);
        assert(divider);
        assert(lv_color_eq(lv_obj_get_style_bg_color(divider, LV_PART_MAIN), lv_color_hex(kBorder)));
    } else if (scenario.rfind("wifi-details-", 0) == 0) {
        const char *active_pane = "settings_wifi_tcpip";
        if (scenario == "wifi-details-dns" || scenario == "wifi-details-resize")
            active_pane = "settings_wifi_dns";
        else if (scenario == "wifi-details-hardware")
            active_pane = "settings_wifi_hardware";
        assert_wifi_details_layout(root, active_pane);
        if (scenario == "wifi-details-tcpip") {
            assert_named_label(root, "settings_wifi_ipv4_address", "192.168.8.158");
            assert_named_label(root, "settings_wifi_ipv4_subnet", "255.255.255.0");
            assert_named_label(root, "settings_wifi_ipv4_gateway", "192.168.8.1");
            assert_named_label(root, "settings_wifi_ipv6_address", "fe80::1234/64");
        } else if (scenario == "wifi-details-dns" || scenario == "wifi-details-resize") {
            assert_named_label(root, "settings_wifi_dns_servers", "192.168.8.1");
            assert_named_label(root, "settings_wifi_dns_search_domains", "lan");
        } else if (scenario == "wifi-details-hardware") {
            assert_named_label(root, "settings_wifi_interface", "wlan0");
            assert_named_label(root, "settings_wifi_mac", "84:2f:57:18:f2:8e");
            assert_named_label(root, "settings_wifi_mtu", "1500");
        }
    } else if (scenario == "wifi-known-connect") {
        auto *known = find_named(root, "settings_wifi_known_group");
        assert(known && find_named(known, "settings_wifi_known_info_1"));
    } else if (scenario == "wifi-password") {
        if (display_width == 320)
            assert_compact_password_title_fits(root);
        else
            assert(find_label(root, "Join Workshop", true));
    } else if (scenario == "wifi-connect" || scenario == "wifi-secured-connect" ||
               scenario == "wifi-keyboard-connect") {
        assert(wait_for_label(root, "CM0 Lab"));
    } else if (scenario == "bluetooth") {
        assert(wait_for_label(root, "Keyboard"));
        assert(wait_for_label(root, "Studio Speaker"));
        lv_obj_t *group = find_named(root, "settings_bluetooth_device_group");
        assert(group && lv_obj_get_height(group) >= 166);
    } else if (scenario == "bluetooth-connect") {
        assert(find_label(root, "Keyboard", true));
    } else if (scenario.rfind("cellular", 0) == 0) {
        assert_current_page(root, "settings_cellular");
        auto *page = find_named(root, "settings_cellular");
        auto *control = find_named(page, "settings_cellular_switch");
        assert(control);
        assert_named_label(root, "settings_cellular_interface", std::getenv("LILYGO_UI_CELLULAR_DEVICE"));
        if (scenario == "cellular-unavailable") {
            assert(wait_for_label(page, "Cellular Modem Unavailable"));
            assert(lv_obj_has_state(control, LV_STATE_DISABLED));
            assert(!lv_obj_has_state(control, LV_STATE_CHECKED));
        } else {
            assert(wait_for_control_enabled(control));
            assert(lv_obj_has_state(control, LV_STATE_CHECKED));
        }
        if (scenario == "cellular-unavailable" || scenario == "cellular-no-sim") {
            assert(wait_for_label(page, scenario == "cellular-no-sim" ? "No SIM Detected" : "Cellular Modem Unavailable"));
            assert_named_label(root, "settings_cellular_sim_status", scenario == "cellular-no-sim" ? "No SIM Detected" : "Unavailable");
            for (const auto *name : {"settings_cellular_iccid", "settings_cellular_imsi",
                                     "settings_cellular_received", "settings_cellular_transmitted", "settings_cellular_total"})
                assert_named_label(root, name, "Unavailable");
        } else {
            assert(wait_for_label(page, scenario == "cellular-denied" ? "Modem Rejected Cellular Change"
                                                                     : "Cellular: Registered"));
            assert_named_label(root, "settings_cellular_sim_status", "Ready");
            assert_named_label(root, "settings_cellular_iccid", "89860012345678901234");
            assert_named_label(root, "settings_cellular_imsi", "460001234567890");
            assert_named_label(root, "settings_cellular_operator", scenario == "cellular-resize" ? kLongCellularOperator : "China Mobile");
            assert_named_label(root, "settings_cellular_technology", "LTE-M");
            assert_named_label(root, "settings_cellular_signal", "-84 dBm RSRP");
            assert_named_label(root, "settings_cellular_interface", std::getenv("LILYGO_UI_CELLULAR_DEVICE"));
            assert_named_label(root, "settings_cellular_received", "1.0 MB");
            assert_named_label(root, "settings_cellular_transmitted", "524.0 kB");
            assert_named_label(root, "settings_cellular_total", "1.6 MB");
            assert(wait_for_label(find_named(page, "settings_cellular_usage_note"),
                                  "Modem-reported totals for its statistics collection period. "
                                  "Collection may be stopped; not carrier billing."));
        }
        assert_cellular_layout(root);
    } else if (scenario == "ethernet" || scenario == "ethernet-config") {
        assert(find_label(root, "1000 Mbps"));
        lv_obj_t *gateway_row = find_named(root, "settings_ethernet_gateway_row");
        assert(gateway_row && find_label(gateway_row, "Gateway"));
        assert(!find_label(find_named(root, "settings_ethernet"), "Router"));
        assert(find_named(root, "settings_ethernet_ipv4_mode"));
        assert(find_named(root, "settings_ethernet_ip_input"));
        assert(find_named(root, "settings_ethernet_subnet_input"));
        assert(find_named(root, "settings_ethernet_gateway_input"));
        assert(find_named(root, "settings_ethernet_dns_input"));
    } else if (scenario.rfind("battery", 0) == 0) {
        assert(find_label(root, "82%"));
        assert(find_named(root, "settings_battery_capacity_apply"));
        if (scenario == "battery-calibration" || scenario == "battery-resize") {
            auto *confirmation = find_named(root, "settings_battery_capacity_confirmation");
            auto *button = find_named(root, "settings_battery_capacity_confirm");
            assert(lv_obj_is_visible(confirmation));
            lv_area_t area, page_area;
            lv_obj_get_coords(button, &area);
            lv_obj_get_coords(find_named(root, "settings_battery"), &page_area);
            assert(area.x1 >= page_area.x1 && area.x2 <= page_area.x2);
            assert(lv_obj_get_height(button) >= 44);
        }
    } else if (scenario == "keyboard") {
        assert(find_label(root, "Key press vibration", true));
        assert(find_named(root, "settings_keyboard_strength_slider"));
        assert(find_label(root, "50%", true));
    } else if (scenario.rfind("serial", 0) == 0) {
        assert_current_page(root, "settings_sharing");
        auto *overlay = find_named(root, "settings_sharing_reboot_overlay");
        if (scenario != "serial-later" && scenario != "serial-now") {
            assert(lv_obj_is_visible(overlay));
            lv_area_t surface_area, overlay_area, dialog_area;
            lv_obj_get_coords(find_named(root, "settings_surface"), &surface_area);
            lv_obj_get_coords(overlay, &overlay_area);
            lv_obj_get_coords(find_named(root, "settings_sharing_reboot_dialog"), &dialog_area);
            assert(overlay_area.x1 == surface_area.x1 && overlay_area.x2 == surface_area.x2);
            assert(overlay_area.y1 == surface_area.y1 && overlay_area.y2 == surface_area.y2);
            for (const auto *name : {"settings_sharing_reboot_now", "settings_sharing_reboot_later"}) {
                auto *button = find_named(root, name);
                lv_area_t area;
                lv_obj_get_coords(button, &area);
                assert(area.x1 >= dialog_area.x1 && area.x2 <= dialog_area.x2);
                assert(area.y1 >= dialog_area.y1 && area.y2 <= dialog_area.y2);
                assert(area.y1 >= surface_area.y1 && area.y2 <= surface_area.y2);
                assert(lv_obj_get_height(button) >= 44);
            }
        }
    } else if (scenario.rfind("sharing", 0) == 0) {
        assert_current_page(root, "settings_sharing");
        if (scenario == "sharing-denied")
            assert(find_label(root, "Administrator Permission Is Required to Change Remote Login."));
        auto *control = find_named(root, "settings_sharing_remote_login_switch");
        auto *title   = find_label(root, "Remote Login", true);
        assert(control && title && lv_obj_is_visible(control));
        lv_area_t title_area, control_area, row_area;
        lv_obj_get_coords(title, &title_area);
        lv_obj_get_coords(control, &control_area);
        lv_obj_get_content_coords(lv_obj_get_parent(control), &row_area);
        assert(title_area.x2 < control_area.x1);
        assert(control_area.x2 <= row_area.x2);
        lv_point_t text_size;
        lv_text_get_size(&text_size, "Remote Login", lv_obj_get_style_text_font(title, LV_PART_MAIN), 0, 0,
                         LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        assert(lv_obj_get_width(title) >= text_size.x);
        auto *serial_title   = find_label(root, "Serial Console");
        auto *serial_control = find_named(root, "settings_sharing_serial_switch");
        assert(serial_title && serial_control);
        lv_obj_get_coords(serial_title, &title_area);
        lv_obj_get_coords(serial_control, &control_area);
        assert(title_area.x2 < control_area.x1);
        lv_text_get_size(&text_size, "Serial Console", lv_obj_get_style_text_font(serial_title, LV_PART_MAIN), 0, 0,
                         lv_obj_get_width(serial_title), LV_TEXT_FLAG_NONE);
        assert(lv_obj_get_height(serial_title) >= text_size.y);
    } else if (scenario == "general") {
        assert(find_label(root, "Sharing", true));
        assert(find_label(root, "About", true));
    } else if (scenario == "about") {
        assert(find_label(root, "LILYGO CM0", true));
    } else if (scenario == "resize-state" || scenario == "lifecycle" || scenario == "reopen-resize") {
        lv_obj_t *input = find_named(root, "settings_password_input");
        assert(input && std::string(lv_textarea_get_text(input)) == "state-survives-resize");
        assert_current_page(root, "settings_wifi");
    }
}

void assert_landscape_content_reachable(const std::string &scenario, lv_obj_t *root)
{
    if (display_width <= display_height) return;

    lv_obj_t *sidebar = find_named(root, "settings_landscape_sidebar");
    lv_obj_t *content = find_named(root, "settings_content");
    assert(sidebar && content && lv_obj_is_visible(sidebar) && lv_obj_is_visible(content));
    lv_area_t sidebar_area;
    lv_area_t content_area;
    lv_obj_get_coords(sidebar, &sidebar_area);
    lv_obj_get_coords(content, &content_area);
    assert(sidebar_area.x1 < content_area.x1);
    assert(sidebar_area.x2 < content_area.x2);
    assert(content_area.x1 > sidebar_area.x1);
    assert(lv_obj_get_width(sidebar) > 0 && lv_obj_get_width(content) > 0);

    auto *connectivity_group = find_named(root, "settings_landscape_connectivity_group");
    auto *general_group      = find_named(root, "settings_landscape_general_group");
    auto *keyboard_group     = find_named(root, "settings_landscape_keyboard_group");
    assert(connectivity_group && general_group && keyboard_group);
    assert(lv_obj_get_child_count(connectivity_group) == 5);
    for (const char *name : {"settings_landscape_wifi_row", "settings_landscape_bluetooth_row",
                             "settings_landscape_ethernet_row", "settings_landscape_cellular_row", "settings_landscape_battery_row"})
        assert(lv_obj_get_parent(find_named(root, name)) == connectivity_group);
    assert(lv_obj_get_parent(find_named(root, "settings_landscape_general_row")) == general_group);
    assert(lv_obj_get_parent(find_named(root, "settings_landscape_keyboard_row")) == keyboard_group);

    const char *page_name = nullptr;
    if (scenario == "home")
        page_name = "settings_home";
    else if (scenario == "wifi" || scenario.rfind("wifi-details-", 0) == 0)
        page_name = "settings_wifi";
    else if (scenario == "bluetooth")
        page_name = "settings_bluetooth";
    else if (scenario == "ethernet")
        page_name = "settings_ethernet";
    else if (scenario.rfind("cellular", 0) == 0)
        page_name = "settings_cellular";
    else if (scenario.rfind("battery", 0) == 0)
        page_name = "settings_battery";
    else if (scenario == "keyboard")
        page_name = "settings_keyboard";
    else if (scenario == "general")
        page_name = "settings_general";
    else if (scenario.rfind("sharing", 0) == 0)
        page_name = "settings_sharing";
    else if (scenario == "about")
        page_name = "settings_about";
    if (page_name) {
        lv_obj_t *page = find_named(root, page_name);
        assert(page && lv_obj_is_visible(page));
        lv_area_t page_area;
        lv_obj_get_coords(page, &page_area);
        assert(page_area.x1 >= content_area.x1 && page_area.x2 <= content_area.x2);
    }

    if (scenario == "home")
        assert(find_named(root, "settings_home_landscape_welcome") &&
               lv_obj_is_visible(find_named(root, "settings_home_landscape_welcome")));
    else if (scenario == "wifi")
        assert_row_reachable(root, "settings_wifi", "Workshop");
    else if (scenario == "bluetooth")
        assert_row_reachable(root, "settings_bluetooth", "Studio Speaker");
    else if (scenario == "ethernet" || scenario == "ethernet-config") {
        assert_row_reachable(root, "settings_ethernet", "MTU");
        lv_area_t details_area;
        lv_obj_get_coords(find_named(root, "settings_ethernet_ipv4_row"), &details_area);
        assert(details_area.x2 >= content_area.x2 - 48);
    } else if (scenario.rfind("cellular", 0) == 0)
        assert_row_reachable(root, "settings_cellular", "Total");
    else if (scenario.rfind("battery", 0) == 0)
        assert_row_reachable(root, "settings_battery", "Temperature");
    else if (scenario == "keyboard")
        assert_row_reachable(root, "settings_keyboard", "Key press vibration");
    else if (scenario == "general")
        assert_row_reachable(root, "settings_general", "About");
    else if (scenario.rfind("sharing", 0) == 0)
        assert_row_reachable(root, "settings_sharing", "Serial Console");
    else if (scenario == "about")
        assert_row_reachable(root, "settings_about", "Architecture");
}

bool has_rendered_contrast()
{
    const auto first = framebuffer.front();
    for (int y = 0; y < display_height; ++y) {
        for (int x = 0; x < display_width; ++x) {
            const auto &pixel =
                framebuffer[static_cast<std::size_t>(y) * kMaximumDimension + static_cast<std::size_t>(x)];
            if (pixel.red != first.red || pixel.green != first.green || pixel.blue != first.blue ||
                pixel.alpha != first.alpha)
                return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc != 3 && argc != 5) {
        std::fprintf(stderr, "usage: %s PAGE OUTPUT.ppm [WIDTH HEIGHT]\n", argv[0]);
        return 2;
    }
    if (argc == 5) {
        display_width  = std::atoi(argv[3]);
        display_height = std::atoi(argv[4]);
        if (display_width < 320 || display_width > kMaximumDimension || display_height < 320 ||
            display_height > kMaximumDimension) {
            std::fprintf(stderr, "viewport must be between 320 and 1232 pixels\n");
            return 2;
        }
    }

    const std::string scenario = argv[1];
    SettingsFixture fixture;
    if (scenario == "battery-unsupported")
        std::filesystem::remove(fixture.path("power/BAT0/calibration_capacity"));
    if (scenario == "cellular-unavailable") fixture.write("cellular-unavailable", "");
    if (scenario == "cellular-no-sim") fixture.write("cellular-no-sim", "");
    if (scenario == "cellular-resize") fixture.write("cellular-operator", kLongCellularOperator);
    fixture.export_environment();
    const bool close_active_command = scenario == "close-active-command";
    if (close_active_command)
        assert(setenv("CM0_TEST_HANG_COMMAND", "1", 1) == 0);
    else
        assert(unsetenv("CM0_TEST_HANG_COMMAND") == 0);

    framebuffer.resize(static_cast<std::size_t>(kMaximumDimension) * kMaximumDimension);
    std::vector<lv_color32_t> draw_buffer(static_cast<std::size_t>(kMaximumDimension) * kPartialRows);

    lv_init();
    lv_display_t *display = lv_display_create(display_width, display_height);
    assert(display);
    lv_display_set_buffers(display, draw_buffer.data(), nullptr,
                           static_cast<std::uint32_t>(draw_buffer.size() * sizeof(lv_color32_t)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_display);
    lv_display_set_default(display);

    lv_obj_t *runtime_screen = lv_screen_active();
    lv_obj_t *root           = create_runtime_root();
    cm0_app_context_t context{};
    context.root                           = root;
    const cm0_app_descriptor_t *descriptor = cm0_app_get_descriptor();
    assert(descriptor && descriptor->api_version == CM0_APP_API_VERSION);
    assert(std::string(descriptor->id) == LILYGO_UI_SETTINGS_APP_ID);
    const std::size_t baseline_timers = timer_count();

    lv_obj_t *surface = open_application(descriptor, context, runtime_screen, baseline_timers);
    assert_initial_visual_contract(root);

    if (scenario == "lifecycle" || scenario == "reopen-resize") {
        close_application(descriptor, context, runtime_screen, surface, baseline_timers, false);
        surface = open_application(descriptor, context, runtime_screen, baseline_timers);
        assert_initial_visual_contract(root);
    }

    run_scenario(scenario, display, root, runtime_screen, fixture);
    pump_timers();
    assert(lv_screen_active() == runtime_screen);
    assert_scenario(scenario, root);

    lv_tick_inc(20);
    lv_refr_now(display);
    assert(has_rendered_contrast());
    write_ppm(argv[2]);
    assert_landscape_content_reachable(scenario, root);

    close_application(descriptor, context, runtime_screen, surface, baseline_timers, close_active_command);
    lv_display_delete(display);
    lilygo_ui_fonts_deinit();
    lv_deinit();
    assert(unsetenv("CM0_TEST_HANG_COMMAND") == 0);
    return 0;
}
