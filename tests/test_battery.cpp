#include "pages/battery/battery_model.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

using lilygo::settings::BatteryCalibrationPhase;
using lilygo::settings::BatteryModel;

namespace {

class BatteryFixture {
public:
    BatteryFixture()
    {
        char pattern[]     = "/tmp/lilygo-ui-battery-test-XXXXXX";
        const auto created = mkdtemp(pattern);
        assert(created);
        root = created;
    }
    ~BatteryFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    void write(const std::string &relative, const std::string &value, mode_t mode = 0644)
    {
        const auto path = root / relative;
        std::filesystem::create_directories(path.parent_path());
        if (std::filesystem::exists(path)) assert(chmod(path.c_str(), 0644) == 0);
        std::ofstream output(path);
        output << value << '\n';
        output.close();
        assert(output);
        assert(chmod(path.c_str(), mode) == 0);
    }
    std::string read(const std::string &relative)
    {
        std::ifstream input(root / relative);
        std::string value;
        std::getline(input, value);
        return value;
    }
    void gauge()
    {
        write("bq27220-0/type", "Battery");
        write("bq27220-0/present", "1");
        write("bq27220-0/status", "Discharging");
        write("bq27220-0/health", "Good");
        write("bq27220-0/technology", "Li-ion");
        write("bq27220-0/capacity", "82");
        write("bq27220-0/cycle_count", "42");
        write("bq27220-0/voltage_now", "4010000");
        write("bq27220-0/current_now", "-225000");
        write("bq27220-0/temp", "315");
        write("bq27220-0/charge_full", "4200000");
        write("bq27220-0/charge_full_design", "5000000");
        write("bq27220-0/charge_now", "3444000");
        write("bq27220-0/time_to_empty_now", "3600");
    }
    void charger()
    {
        write("bq25890-charger-0/type", "USB");
        write("bq25890-charger-0/model_name", "BQ25890");
        write("bq25890-charger-0/online", "1");
        write("bq25890-charger-0/status", "Charging");
        write("bq25890-charger-0/health", "Good");
        write("bq25890-charger-0/input_current_limit", "1500000");
        write("bq25890-charger-0/constant_charge_current", "1280000");
        write("bq25890-charger-0/constant_charge_current_max", "1984000", 0444);
        write("bq25890-charger-0/constant_charge_voltage", "4192000");
        write("bq25890-charger-0/constant_charge_voltage_max", "4192000", 0444);
    }
    std::filesystem::path root;
};

void test_identification_and_telemetry()
{
    BatteryFixture fixture;
    fixture.write("BAT0/type", "Battery");
    fixture.write("BAT0/capacity", "10");
    fixture.gauge();
    fixture.charger();
    BatteryModel model(fixture.root.string());
    model.refresh();
    const auto &battery = model.battery();
    assert(battery.available);
    assert(battery.supply_name == "bq27220-0");
    assert(battery.model_name == "BQ27220");
    assert(battery.status == "Discharging");
    assert(battery.health == "Good");
    assert(battery.has_capacity && battery.capacity_percent == 82);
    assert(battery.has_current && battery.current_ua == -225000);
    assert(battery.has_voltage && battery.voltage_uv == 4010000);
    assert(battery.has_charge_full && battery.charge_full_uah == 4200000);
    assert(battery.has_charge_full_design && battery.charge_full_design_uah == 5000000);
    assert(battery.has_charge_now && battery.charge_now_uah == 3444000);
    assert(battery.has_time_to_empty && battery.time_to_empty_seconds == 3600);
    assert(!battery.has_time_to_full);
    assert(battery.has_cycle_count && battery.cycle_count == 42);
    assert(battery.has_temperature && battery.temperature_tenths_c == 315);
    assert(model.charger().available && model.charger().online);
    assert(model.charger().charge_current.maximum == 1984000);
    assert(model.charger().charge_current.step == 64000);
    assert(model.charger().charge_voltage.available && !model.charger().charge_voltage.writable);

    fixture.write("bq27220-0/present", "0");
    model.refresh();
    assert(model.battery().supply_name == "BAT0");
    assert(model.battery().capacity_percent == 10);
    assert(!model.calibration().available);
}

void test_invalid_and_missing_readings()
{
    BatteryFixture fixture;
    fixture.gauge();
    fixture.write("bq27220-0/capacity", "101");
    fixture.write("bq27220-0/cycle_count", "-1");
    fixture.write("bq27220-0/voltage_now", "123junk");
    fixture.write("bq27220-0/current_now", "99999999999999999999999999");
    fixture.write("bq27220-0/charge_full", "-1");
    fixture.write("bq27220-0/temp", "-52");
    std::filesystem::remove(fixture.root / "bq27220-0/charge_full_design");
    BatteryModel model(fixture.root.string());
    model.refresh();
    const auto &battery = model.battery();
    assert(battery.available);
    assert(!battery.has_capacity && !battery.has_cycle_count && !battery.has_voltage && !battery.has_current);
    assert(!battery.has_charge_full && !battery.has_charge_full_design);
    assert(battery.has_temperature && battery.temperature_tenths_c == -52);
    assert(!model.start_calibration().success);
    std::filesystem::remove_all(fixture.root / "bq27220-0");
    model.refresh();
    assert(!model.battery().available && !model.battery().has_temperature);
    assert(!model.charger().available);
    assert(!model.set_charge_current(64000).success);
    assert(!std::filesystem::exists(fixture.root / "constant_charge_current"));
}

void test_charger_values_are_checked_written_and_read_back()
{
    BatteryFixture fixture;
    fixture.charger();
    BatteryModel model(fixture.root.string());
    assert(model.set_charge_current(640000).success);
    assert(fixture.read("bq25890-charger-0/constant_charge_current") == "640000");
    assert(model.charger().charge_current.value == 640000);
    assert(model.set_charge_current(1984000).success);
    assert(model.set_charge_current(0).success);
    assert(!model.set_charge_current(2000000).success);
    assert(!model.set_charge_current(-64000).success);
    assert(!model.set_charge_current(1000000).success);
    assert(fixture.read("bq25890-charger-0/constant_charge_current") == "0");
    assert(model.set_input_current_limit(100000).success);
    assert(model.set_input_current_limit(3250000).success);
    assert(!model.set_input_current_limit(3300000).success);
    assert(!model.set_input_current_limit(99999).success);
    assert(!model.set_input_current_limit(125000).success);
    assert(fixture.read("bq25890-charger-0/input_current_limit") == "3250000");
    assert(fixture.read("bq25890-charger-0/online") == "1");
    assert(fixture.read("bq25890-charger-0/constant_charge_voltage") == "4192000");
}

void test_driver_capabilities_and_limits_are_required()
{
    BatteryFixture fixture;
    fixture.charger();
    fixture.write("bq25890-charger-0/constant_charge_current", "1280000", 0444);
    fixture.write("bq25890-charger-0/input_current_limit", "1500000", 0444);
    BatteryModel model(fixture.root.string());
    model.refresh();
    assert(model.charger().charge_current.available && !model.charger().charge_current.writable);
    assert(!model.set_charge_current(640000).success);
    assert(!model.set_input_current_limit(500000).success);
    fixture.write("bq25890-charger-0/constant_charge_current", "1280000");
    std::filesystem::remove(fixture.root / "bq25890-charger-0/constant_charge_current_max");
    assert(!model.set_charge_current(640000).success);
    fixture.write("bq25890-charger-0/constant_charge_current_max", "bogus");
    assert(!model.set_charge_current(640000).success);
    std::filesystem::remove(fixture.root / "bq25890-charger-0/input_current_limit");
    assert(!model.set_input_current_limit(500000).success);
    assert(!std::filesystem::exists(fixture.root / "bq25890-charger-0/input_current_limit"));
    fixture.write("bq25890-charger-0/input_current_limit", "500000");
    std::filesystem::rename(fixture.root / "bq25890-charger-0", fixture.root / "unrelated-charger");
    fixture.write("unrelated-charger/model_name", "Other charger");
    model.refresh();
    assert(!model.charger().available);
    assert(!model.set_input_current_limit(600000).success);
}

void test_guided_cycle_never_claims_hardware_calibration()
{
    BatteryFixture fixture;
    fixture.gauge();
    BatteryModel model(fixture.root.string());
    assert(model.start_calibration().success);
    assert(model.calibration().phase == BatteryCalibrationPhase::charge);
    fixture.write("bq27220-0/capacity", "100");
    fixture.write("bq27220-0/status", "Charging");
    model.refresh();
    assert(model.calibration().phase == BatteryCalibrationPhase::charge);
    fixture.write("bq27220-0/status", "Full");
    model.refresh();
    assert(model.calibration().phase == BatteryCalibrationPhase::discharge);
    fixture.write("bq27220-0/status", "Discharging");
    fixture.write("bq27220-0/capacity", "10");
    model.refresh();
    assert(model.calibration().phase == BatteryCalibrationPhase::recharge);
    fixture.write("bq27220-0/status", "Charging");
    fixture.write("bq27220-0/capacity", "100");
    model.refresh();
    assert(model.calibration().active);
    fixture.write("bq27220-0/status", "Full");
    model.refresh();
    assert(!model.calibration().active);
    assert(model.calibration().phase == BatteryCalibrationPhase::complete);
    assert(model.calibration().message.find("does not report") != std::string::npos);
    assert(!std::filesystem::exists(fixture.root / "bq27220-0/calibrate"));
    assert(fixture.read("bq27220-0/charge_full") == "4200000");
}

void test_guided_cycle_interruptions()
{
    BatteryFixture fixture;
    fixture.gauge();
    fixture.write("bq27220-0/status", "Full");
    fixture.write("bq27220-0/capacity", "100");
    BatteryModel model(fixture.root.string());
    assert(model.start_calibration().success);
    fixture.write("bq27220-0/status", "Discharging");
    fixture.write("bq27220-0/capacity", "50");
    model.refresh();
    fixture.write("bq27220-0/status", "Charging");
    model.refresh();
    assert(!model.calibration().active && model.calibration().phase == BatteryCalibrationPhase::interrupted);
    assert(model.start_calibration().success);
    fixture.write("bq27220-0/capacity", "invalid");
    model.refresh();
    assert(!model.calibration().active && model.calibration().phase == BatteryCalibrationPhase::interrupted);
    fixture.write("bq27220-0/capacity", "50");
    assert(model.start_calibration().success);
    fixture.write("bq27220-0/serial_number", "different-battery");
    model.refresh();
    assert(!model.calibration().active && model.calibration().phase == BatteryCalibrationPhase::interrupted);
    assert(model.start_calibration().success);
    model.cancel_calibration();
    assert(!model.calibration().active && model.calibration().phase == BatteryCalibrationPhase::idle);
    fixture.write("bq27220-0/health", "Overheat");
    assert(!model.start_calibration().success);
}

void test_capacity_configuration_requires_verified_driver_extension()
{
    BatteryFixture fixture;
    fixture.gauge();
    BatteryModel model(fixture.root.string());
    model.refresh();
    assert(!model.capacity_configuration().available);
    assert(!model.set_capacity_configuration(3000, 2800).success);
    assert(!std::filesystem::exists(fixture.root / "bq27220-0/calibration_capacity"));
    assert(fixture.read("bq27220-0/charge_full_design") == "5000000");
    assert(fixture.read("bq27220-0/charge_full") == "4200000");

    fixture.write("bq27220-0/calibration_capacity", "5000 4200", 0444);
    model.refresh();
    assert(model.capacity_configuration().available && !model.capacity_configuration().writable);
    assert(model.capacity_configuration().design_mah == 5000);
    assert(model.capacity_configuration().full_mah == 4200);
    assert(!model.set_capacity_configuration(3000, 2800).success);
    assert(fixture.read("bq27220-0/calibration_capacity") == "5000 4200");

    for (const auto &text : {"5000", "5000 4200 trailing", "5000 65536", "-1 4200", "bad 4200",
                             "99999999999999999999999 4200"}) {
        fixture.write("bq27220-0/calibration_capacity", text);
        model.refresh();
        assert(!model.capacity_configuration().available);
        assert(!model.set_capacity_configuration(3000, 2800).success);
    }
}

void test_capacity_pair_is_applied_and_confirmed_in_mah()
{
    BatteryFixture fixture;
    fixture.gauge();
    fixture.write("bq27220-0/calibration_capacity", "5000 4200");
    BatteryModel model(fixture.root.string());
    assert(model.start_calibration().success);
    assert(model.set_capacity_configuration(3000, 2800).success);
    assert(fixture.read("bq27220-0/calibration_capacity") == "3000 2800");
    assert(model.capacity_configuration().design_mah == 3000);
    assert(model.capacity_configuration().full_mah == 2800);
    assert(!model.calibration().active);
    assert(model.calibration().phase == BatteryCalibrationPhase::interrupted);
    assert(model.set_capacity_configuration(3000, 3100).success);  // FCC may exceed the nominal design.
    assert(model.set_capacity_configuration(32767, 32767).success);
    assert(model.set_capacity_configuration(1, 1).success);
    assert(!model.set_capacity_configuration(0, 1000).success);
    assert(!model.set_capacity_configuration(3000, -1).success);
    assert(!model.set_capacity_configuration(32768, 3000).success);
    assert(!model.set_capacity_configuration(3000, 32768).success);
    assert(fixture.read("bq27220-0/calibration_capacity") == "1 1");
    fixture.write("bq27220-0/calibration_capacity", "0 0");
    model.refresh();
    assert(model.capacity_configuration().available && model.capacity_configuration().writable);
    assert(model.set_capacity_configuration(3000, 2800).success);
    fixture.write("bq27220-0/calibration_capacity", "65535 65535");
    model.refresh();
    assert(model.capacity_configuration().available);
    assert(!model.set_capacity_configuration(65535, 65535).success);
    assert(model.set_capacity_configuration(3000, 2800).success);
}

void test_control_writes_do_not_follow_attribute_symlinks()
{
    BatteryFixture fixture;
    fixture.gauge();
    fixture.charger();
    fixture.write("pair", "3000 2800");
    std::filesystem::create_symlink(fixture.root / "pair", fixture.root / "bq27220-0/calibration_capacity");
    fixture.write("limit", "1500000");
    std::filesystem::remove(fixture.root / "bq25890-charger-0/input_current_limit");
    std::filesystem::create_symlink(fixture.root / "limit", fixture.root / "bq25890-charger-0/input_current_limit");
    BatteryModel model(fixture.root.string());
    assert(!model.set_capacity_configuration(5000, 4200).success);
    assert(!model.set_input_current_limit(500000).success);
    assert(fixture.read("pair") == "3000 2800");
    assert(fixture.read("limit") == "1500000");
}

void test_device_replacement_invalidates_previous_snapshot()
{
    BatteryFixture fixture;
    fixture.gauge();
    fixture.charger();
    fixture.write("bq27220-0/calibration_capacity", "5000 4200");
    BatteryModel model(fixture.root.string());
    model.refresh();
    const auto gauge_identity   = model.battery().identity;
    const auto charger_identity = model.charger().identity;
    const auto gauge_token      = model.battery().control_token;
    const auto charger_token    = model.charger().control_token;
    assert(!gauge_identity.empty() && !charger_identity.empty());
    assert(gauge_token.find(':') != std::string::npos && charger_token.find(':') != std::string::npos);
    model.refresh();
    assert(model.battery().identity == gauge_identity);
    assert(model.charger().identity == charger_identity);
    assert(model.start_calibration().success);

    // Keep the old directory alive to guarantee the replacement gets a new
    // inode, even though its public supply name and model name are identical.
    std::filesystem::rename(fixture.root / "bq27220-0", fixture.root / "detached-gauge");
    fixture.gauge();
    fixture.write("bq27220-0/calibration_capacity", "3000 2800");
    assert(!model.set_capacity_configuration(4000, 3800).success);
    assert(model.battery().identity != gauge_identity);
    assert(model.battery().control_token != gauge_token);
    assert(!model.set_capacity_configuration(4000, 3800, gauge_token).success);
    assert(fixture.read("bq27220-0/calibration_capacity") == "3000 2800");
    assert(!model.calibration().active && model.calibration().phase == BatteryCalibrationPhase::interrupted);

    std::filesystem::rename(fixture.root / "bq25890-charger-0", fixture.root / "detached-charger");
    fixture.write("detached-charger/model_name", "Detached");
    fixture.charger();
    assert(!model.set_charge_current(640000).success);
    assert(model.charger().identity != charger_identity);
    assert(model.charger().control_token != charger_token);
    assert(!model.set_charge_current(640000, charger_token).success);
    assert(!model.set_input_current_limit(500000, charger_token).success);
    assert(fixture.read("bq25890-charger-0/constant_charge_current") == "1280000");
    assert(model.set_charge_current(640000, model.charger().control_token).success);
    assert(model.set_input_current_limit(500000, model.charger().control_token).success);
    assert(model.set_capacity_configuration(4000, 3800, model.battery().control_token).success);
}

}  // namespace

int main()
{
    test_identification_and_telemetry();
    test_invalid_and_missing_readings();
    test_charger_values_are_checked_written_and_read_back();
    test_driver_capabilities_and_limits_are_required();
    test_guided_cycle_never_claims_hardware_calibration();
    test_guided_cycle_interruptions();
    test_capacity_configuration_requires_verified_driver_extension();
    test_capacity_pair_is_applied_and_confirmed_in_mah();
    test_control_writes_do_not_follow_attribute_symlinks();
    test_device_replacement_invalidates_previous_snapshot();
}
