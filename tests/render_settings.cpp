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

void assert_current_page(lv_obj_t *root, const char *current_name)
{
    constexpr const char *page_names[] = {
        "settings_home",    "settings_wifi",    "settings_bluetooth", "settings_ethernet",
        "settings_battery", "settings_general", "settings_about",
    };
    for (const char *name : page_names) {
        lv_obj_t *page = find_named(root, name);
        assert(page && is_descendant_of(page, root));
        assert(lv_obj_has_flag(page, LV_OBJ_FLAG_HIDDEN) != (std::strcmp(name, current_name) == 0));
    }
}

void assert_menu_row_content_centered(lv_obj_t *root, const char *row_name, const char *title)
{
    lv_obj_t *row = find_named(root, row_name);
    assert(row && lv_obj_get_child_count(row) == 5);

    lv_obj_t *label = lv_obj_get_child(row, 1);
    assert(label && lv_obj_check_type(label, &lv_label_class));

    lv_obj_t *icon = lv_obj_get_child(row, 0);
    assert(icon && lv_obj_check_type(icon, &lv_image_class));
    assert(lv_obj_get_width(icon) == 42 && lv_obj_get_height(icon) == 42);

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
    lv_obj_t *wifi_title     = find_label(root, "Wi-Fi", true);
    lv_obj_t *wifi_status    = find_named(root, "settings_wifi_status");
    lv_obj_t *ethernet_state = find_named(root, "settings_ethernet_status");
    lv_obj_t *battery_level  = find_named(root, "settings_battery_percent");
    lv_obj_t *ipv4_row       = find_named(root, "settings_ethernet_ipv4_row");
    lv_obj_t *password_input = find_named(root, "settings_password_input");
    lv_obj_t *keyboard       = find_named(root, "settings_password_keyboard");
    assert(home && settings_title && wifi_title && wifi_status && ethernet_state && battery_level && ipv4_row &&
           password_input && keyboard);
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
    click_named(root, "settings_home_wifi_row");
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

void run_scenario(const std::string &scenario, lv_display_t *display, lv_obj_t *root, lv_obj_t *runtime_screen,
                  const SettingsFixture &fixture)
{
    if (scenario == "home" || scenario == "close-active-command") return;
    if (scenario == "wifi") {
        click_named(root, "settings_home_wifi_row");
        assert_current_page(root, "settings_wifi");
    } else if (scenario == "wifi-password") {
        enter_password_page(root, display_width == 320 ? kLongestWifiSsid : "Workshop");
    } else if (scenario == "wifi-connect") {
        click_named(root, "settings_home_wifi_row");
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
    } else if (scenario == "bluetooth" || scenario == "bluetooth-connect") {
        assert(wait_for_label(root, "CM0 Lab"));
        click_named(root, "settings_home_bluetooth_row");
        assert_current_page(root, "settings_bluetooth");
        if (scenario == "bluetooth-connect") {
            assert(wait_for_label(root, "Keyboard"));
            click_label(root, "Keyboard");
            assert(wait_for_command(fixture, "connect AA:BB:CC:DD:EE:01"));
        }
    } else if (scenario == "ethernet") {
        click_named(root, "settings_home_ethernet_row");
        assert_current_page(root, "settings_ethernet");
    } else if (scenario == "battery") {
        click_named(root, "settings_home_battery_row");
        assert_current_page(root, "settings_battery");
    } else if (scenario == "general") {
        click_named(root, "settings_home_general_row");
        assert_current_page(root, "settings_general");
        click_named(root, "settings_general_about_row");
        assert_current_page(root, "settings_about");
        click_named(root, "settings_about_back");
        assert_current_page(root, "settings_general");
    } else if (scenario == "about") {
        click_named(root, "settings_home_general_row");
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
        assert_menu_row_content_centered(root, "settings_home_battery_row", "Battery");
        assert_menu_row_content_centered(root, "settings_home_general_row", "General");
        if (display_width < 400 && display_height < 720) assert_compact_home_avoids_system_area(root);
    } else if (scenario == "close-active-command") {
        assert_current_page(root, "settings_home");
        assert(find_label(root, "Settings", true));
    } else if (scenario == "wifi") {
        assert(wait_for_label(root, "CM0 Lab"));
        assert(wait_for_label(root, "Workshop"));
        lv_obj_t *group = find_named(root, "settings_wifi_network_group");
        assert(group && lv_obj_get_height(group) >= 166);
        lv_obj_t *workshop = find_label(group, "Workshop");
        assert(workshop);
        lv_obj_t *row     = lv_obj_get_parent(workshop);
        lv_obj_t *divider = lv_obj_get_child(row, 5);
        assert(divider);
        assert(lv_color_eq(lv_obj_get_style_bg_color(divider, LV_PART_MAIN), lv_color_hex(kBorder)));
    } else if (scenario == "wifi-password") {
        if (display_width == 320)
            assert_compact_password_title_fits(root);
        else
            assert(find_label(root, "Join Workshop", true));
    } else if (scenario == "wifi-connect" || scenario == "wifi-secured-connect") {
        assert(wait_for_label(root, "CM0 Lab"));
    } else if (scenario == "bluetooth") {
        assert(wait_for_label(root, "Keyboard"));
        assert(wait_for_label(root, "Studio Speaker"));
        lv_obj_t *group = find_named(root, "settings_bluetooth_device_group");
        assert(group && lv_obj_get_height(group) >= 166);
    } else if (scenario == "bluetooth-connect") {
        assert(find_label(root, "Keyboard", true));
    } else if (scenario == "ethernet") {
        assert(find_label(root, "1000 Mbps"));
    } else if (scenario == "battery") {
        assert(find_label(root, "82%", true));
    } else if (scenario == "general") {
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
    if (scenario == "home")
        assert_row_reachable(root, "settings_home", "General");
    else if (scenario == "wifi")
        assert_row_reachable(root, "settings_wifi", "Workshop");
    else if (scenario == "bluetooth")
        assert_row_reachable(root, "settings_bluetooth", "Studio Speaker");
    else if (scenario == "ethernet")
        assert_row_reachable(root, "settings_ethernet", "MTU");
    else if (scenario == "battery")
        assert_row_reachable(root, "settings_battery", "Temperature");
    else if (scenario == "general")
        assert_row_reachable(root, "settings_general", "About");
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
