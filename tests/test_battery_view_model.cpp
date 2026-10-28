#include "pages/battery/battery_view_model.hpp"
#include "settings_fixture.hpp"

#include <lvgl.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using namespace lilygo::settings;
using lilygo::settings::test_support::SettingsFixture;

std::int64_t read_number(const std::string &path)
{
    std::ifstream stream(path);
    std::int64_t number = 0;
    stream >> number;
    assert(stream);
    return number;
}

void test_current_limits_require_apply_and_preserve_draft()
{
    SettingsFixture fixture;
    SettingsModel model(fixture.environment());
    model.refresh_battery();
    BatteryViewModel view_model(model);
    view_model.publish();
    assert(view_model.presentation().percent == "82%");
    assert(view_model.presentation().remaining_charge == "3444 mAh");
    assert(view_model.presentation().full_charge == "4200 mAh");
    assert(view_model.presentation().design_charge == "5000 mAh");
    assert(view_model.presentation().input_current.enabled);
    const auto input_path = fixture.path("power/bq25890-charger/input_current_limit");
    const auto before     = read_number(input_path);
    view_model.stage_input_current(4);
    assert(view_model.presentation().input_current.pending);
    assert(view_model.presentation().input_current.value == "300 mA (not applied)");
    assert(read_number(input_path) == before);
    model.refresh_battery();
    view_model.publish();
    assert(view_model.presentation().input_current.position == 4);
    view_model.apply_input_current();
    assert(read_number(input_path) == 300000);
    assert(view_model.presentation().input_current.value == "300 mA");
    assert(!view_model.presentation().input_current.pending);
    assert(!view_model.busy());

    const auto charge_path = fixture.path("power/bq25890-charger/constant_charge_current");
    const auto max_path    = fixture.path("power/bq25890-charger/constant_charge_current_max");
    const auto maximum     = read_number(max_path);
    view_model.stage_charge_current(10);
    assert(view_model.presentation().charge_current.pending);
    view_model.apply_charge_current();
    assert(read_number(charge_path) == 640000);
    assert(read_number(max_path) == maximum);
    assert(view_model.presentation().charge_current.value == "640 mA");
}

void test_unavailable_controls_do_not_mutate_files()
{
    SettingsFixture fixture;
    fixture.write("power/bq25890-charger/input_current_limit", "500000\n", 0400);
    SettingsModel model(fixture.environment());
    model.refresh_battery();
    BatteryViewModel view_model(model);
    view_model.publish();
    assert(view_model.presentation().input_current.available);
    assert(!view_model.presentation().input_current.enabled);
    assert(view_model.presentation().input_current.range.find("Read-only") != std::string::npos);
    view_model.stage_input_current(4);
    view_model.apply_input_current();
    assert(read_number(fixture.path("power/bq25890-charger/input_current_limit")) == 500000);
    assert(!view_model.presentation().input_current.pending);
    std::filesystem::remove_all(fixture.path("power/bq25890-charger"));
    model.refresh_battery();
    view_model.publish();
    assert(view_model.presentation().charger == "Charger Unavailable");
    assert(!view_model.presentation().input_current.available);
    assert(!view_model.presentation().charge_current.available);
    assert(view_model.presentation().percent == "82%");
}

void test_capacity_calibration_requires_valid_pair_and_confirmation()
{
    SettingsFixture fixture;
    fixture.write("power/BAT0/calibration_capacity", "5000 4200\n");
    SettingsModel model(fixture.environment());
    model.refresh_battery();
    BatteryViewModel view_model(model);
    view_model.publish();
    assert(view_model.presentation().capacity_configuration.enabled);
    assert(view_model.presentation().capacity_configuration.design == "5000");
    assert(view_model.presentation().capacity_configuration.full == "4200");
    assert(!view_model.presentation().capacity_configuration.can_apply);
    view_model.stage_capacity("0", "4200");
    assert(!view_model.presentation().capacity_configuration.can_apply);
    assert(view_model.presentation().capacity_configuration.message.find("32767") != std::string::npos);
    view_model.request_capacity_apply();
    assert(!view_model.presentation().capacity_configuration.show_confirmation);
    view_model.stage_capacity("4000", "4500");
    assert(view_model.presentation().capacity_configuration.can_apply);
    model.refresh_battery();
    view_model.publish();
    assert(view_model.presentation().capacity_configuration.design == "4000");
    assert(view_model.presentation().capacity_configuration.full == "4500");
    view_model.confirm_capacity_apply();
    assert(model.battery_details().capacity_configuration().design_mah == 5000);
    view_model.request_capacity_apply();
    assert(view_model.presentation().capacity_configuration.show_confirmation);
    assert(view_model.presentation().capacity_configuration.confirmation.find("4000 mAh") != std::string::npos);
    assert(view_model.presentation().capacity_configuration.confirmation.find("4500 mAh") != std::string::npos);
    view_model.cancel_capacity_apply();
    assert(!view_model.presentation().capacity_configuration.show_confirmation);
    assert(model.battery_details().capacity_configuration().design_mah == 5000);
    view_model.request_capacity_apply();
    view_model.confirm_capacity_apply();
    assert(model.battery_details().capacity_configuration().design_mah == 4000);
    assert(model.battery_details().capacity_configuration().full_mah == 4500);
    assert(!view_model.presentation().capacity_configuration.pending);
    assert(!view_model.presentation().capacity_configuration.show_confirmation);
    view_model.stage_capacity("32768", "4500");
    assert(!view_model.presentation().capacity_configuration.can_apply);
    view_model.stage_capacity("4x", "4500");
    assert(!view_model.presentation().capacity_configuration.can_apply);
    view_model.stage_capacity("", "4500");
    assert(!view_model.presentation().capacity_configuration.can_apply);
}

void test_old_driver_capacity_fields_stay_read_only()
{
    SettingsFixture fixture;
    std::filesystem::remove(fixture.path("power/BAT0/calibration_capacity"));
    SettingsModel model(fixture.environment());
    model.refresh_battery();
    BatteryViewModel view_model(model);
    view_model.publish();
    assert(!view_model.presentation().capacity_configuration.enabled);
    assert(view_model.presentation().capacity_configuration.design == "5000");
    assert(view_model.presentation().capacity_configuration.full == "4200");
    assert(view_model.presentation().capacity_configuration.message.find("update") != std::string::npos);
    view_model.stage_capacity("4000", "4500");
    view_model.request_capacity_apply();
    view_model.confirm_capacity_apply();
    assert(view_model.presentation().capacity_configuration.design == "5000");
    assert(!view_model.presentation().capacity_configuration.pending);
    assert(read_number(fixture.path("power/BAT0/charge_full_design")) == 5000000);
}

void test_replacement_invalidates_capacity_confirmation_and_current_drafts()
{
    SettingsFixture fixture;
    SettingsModel model(fixture.environment());
    model.refresh_battery();
    BatteryViewModel view_model(model);
    view_model.publish();
    view_model.stage_capacity("4000", "4500");
    view_model.request_capacity_apply();
    assert(view_model.presentation().capacity_configuration.show_confirmation);
    const auto original_battery = model.battery().identity;
    std::filesystem::rename(fixture.path("power/BAT0"), fixture.path("removed-battery"));
    fixture.write("power/BAT0/type", "Battery\n");
    fixture.write("power/BAT0/model_name", "bq27220\n");
    fixture.write("power/BAT0/capacity", "75\n");
    fixture.write("power/BAT0/calibration_capacity", "6000 5200\n");
    view_model.confirm_capacity_apply();
    assert(model.battery().identity != original_battery);
    assert(model.battery_details().capacity_configuration().design_mah == 6000);
    assert(model.battery_details().capacity_configuration().full_mah == 5200);
    assert(!view_model.presentation().capacity_configuration.show_confirmation);
    assert(!view_model.presentation().capacity_configuration.pending);
    assert(view_model.presentation().capacity_configuration.design == "6000");

    view_model.stage_input_current(4);
    assert(view_model.presentation().input_current.pending);
    const auto original_charger = model.battery_details().charger().identity;
    std::filesystem::rename(fixture.path("power/bq25890-charger"), fixture.path("removed-charger"));
    fixture.write("power/bq25890-charger/type", "USB\n");
    fixture.write("power/bq25890-charger/model_name", "bq25890\n");
    fixture.write("power/bq25890-charger/input_current_limit", "700000\n");
    view_model.apply_input_current();
    assert(model.battery_details().charger().identity != original_charger);
    assert(read_number(fixture.path("power/bq25890-charger/input_current_limit")) == 700000);
    assert(!view_model.presentation().input_current.pending);
    assert(view_model.presentation().input_current.value == "700 mA");
    assert(view_model.presentation().charging_message.find("changed") != std::string::npos);
}

void test_calibration_requires_confirmation_and_can_be_cancelled()
{
    SettingsFixture fixture;
    SettingsModel model(fixture.environment());
    model.refresh_battery();
    BatteryViewModel view_model(model);
    view_model.publish();
    assert(view_model.presentation().calibration_enabled);
    view_model.confirm_calibration();
    assert(!model.battery_details().calibration().active);
    view_model.request_calibration();
    assert(view_model.presentation().calibration_confirmation);
    assert(!model.battery_details().calibration().active);
    view_model.cancel_calibration();
    assert(!view_model.presentation().calibration_confirmation);
    assert(!model.battery_details().calibration().active);
    view_model.request_calibration();
    view_model.confirm_calibration();
    assert(model.battery_details().calibration().active);
    assert(view_model.presentation().calibration_active);
    assert(!view_model.presentation().calibration_confirmation);
    model.refresh_battery();
    view_model.publish();
    assert(view_model.presentation().calibration_active);
    view_model.cancel_calibration();
    assert(!model.battery_details().calibration().active);
    assert(!view_model.presentation().calibration_active);
}

}  // namespace

int main()
{
    lv_init();
    test_current_limits_require_apply_and_preserve_draft();
    test_unavailable_controls_do_not_mutate_files();
    test_calibration_requires_confirmation_and_can_be_cancelled();
    test_capacity_calibration_requires_valid_pair_and_confirmation();
    test_old_driver_capacity_fields_stay_read_only();
    test_replacement_invalidates_capacity_confirmation_and_current_drafts();
    lv_deinit();
}
