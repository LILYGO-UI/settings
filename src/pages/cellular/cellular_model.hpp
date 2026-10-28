#ifndef LILYGO_UI_SETTINGS_PAGES_CELLULAR_CELLULAR_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_CELLULAR_CELLULAR_MODEL_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace lilygo::settings {

struct CellularEnvironment {
    std::string serial_device = "/dev/ttyACM0";
    static CellularEnvironment from_process_environment();
};

struct CellularState {
    bool radio_known = false;
    bool enabled = false;
    int function_mode = -1;
    bool capabilities_known = false;
    int enable_function = -1;
    int disable_function = -1;
    bool system_mode_known = false;
    bool lte_supported = false;
    bool sim_known = false;
    bool sim_ready = false;
    std::string sim_status;
    std::string iccid;
    std::string imsi;
    std::string operator_name;
    std::string access_technology;
    bool signal_known = false;
    bool signal_is_rsrp = false;
    int signal_dbm = 0;
    int signal_index = -1;
    int signal = -1;
    bool registration_known = false;
    int registration_status = -1;
    bool usage_known = false;
    bool counters_reset = false;
    // Nordic %XCONNSTAT reports decimal kilobytes, converted to bytes here.
    std::uint64_t received = 0;
    std::uint64_t transmitted = 0;
};

// Parses replies from the nRF9151 SLM. No shell commands, network configuration,
// SIM unlock commands, or assumptions about host network interfaces are needed.
class CellularModel {
public:
    explicit CellularModel(CellularEnvironment environment = CellularEnvironment::from_process_environment());
    [[nodiscard]] const CellularState &state() const noexcept;
    [[nodiscard]] bool update_radio(std::string_view output);
    [[nodiscard]] bool update_capabilities(std::string_view output);
    [[nodiscard]] bool update_system_mode(std::string_view output);
    [[nodiscard]] bool update_sim_status(std::string_view output);
    [[nodiscard]] bool update_iccid(std::string_view output);
    [[nodiscard]] bool update_imsi(std::string_view output);
    [[nodiscard]] bool update_operator(std::string_view output);
    [[nodiscard]] bool update_signal(std::string_view output);
    [[nodiscard]] bool update_registration(std::string_view output);
    [[nodiscard]] bool update_usage(std::string_view output);
    void radio_failed() noexcept;
    void capabilities_failed() noexcept;
    void sim_failed() noexcept;
    void operator_failed() noexcept;
    void signal_failed() noexcept;
    void registration_failed() noexcept;
    void clear_usage() noexcept;
    void reset() noexcept;

private:
    void resolve_radio() noexcept;
    CellularState state_;
    bool last_usage_known_ = false;
    std::uint64_t last_received_ = 0;
    std::uint64_t last_transmitted_ = 0;
};

}  // namespace lilygo::settings

#endif
