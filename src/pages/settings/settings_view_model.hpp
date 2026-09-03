#ifndef LILYGO_UI_SETTINGS_PAGES_SETTINGS_SETTINGS_VIEW_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_SETTINGS_SETTINGS_VIEW_MODEL_HPP

#include "domain/settings_command_runner.hpp"
#include "domain/settings_model.hpp"
#include "pages/about/about_view_model.hpp"
#include "pages/battery/battery_view_model.hpp"
#include "pages/bluetooth/bluetooth_view_model.hpp"
#include "pages/ethernet/ethernet_view_model.hpp"
#include "pages/general/general_view_model.hpp"
#include "pages/home/home_view_model.hpp"
#include "pages/wifi/wifi_view_model.hpp"

namespace lilygo::settings {

class SettingsViewModel {
public:
    explicit SettingsViewModel(SettingsEnvironment environment = SettingsEnvironment::from_process_environment());
    ~SettingsViewModel();

    SettingsViewModel(const SettingsViewModel &)            = delete;
    SettingsViewModel &operator=(const SettingsViewModel &) = delete;

    void refresh();
    void poll_command();
    void stop() noexcept;
    [[nodiscard]] bool busy() const noexcept;

    [[nodiscard]] HomeViewModel &home() noexcept;
    [[nodiscard]] WifiViewModel &wifi() noexcept;
    [[nodiscard]] BluetoothViewModel &bluetooth() noexcept;
    [[nodiscard]] EthernetViewModel &ethernet() noexcept;
    [[nodiscard]] BatteryViewModel &battery() noexcept;
    [[nodiscard]] GeneralViewModel &general() noexcept;
    [[nodiscard]] AboutViewModel &about() noexcept;

private:
    void publish_passive_pages();
    void publish_command_state_if_changed();
    void start_next_background_command();

    SettingsModel model_;
    SettingsCommandRunner commands_;
    HomeViewModel home_;
    WifiViewModel wifi_;
    BluetoothViewModel bluetooth_;
    EthernetViewModel ethernet_;
    BatteryViewModel battery_;
    GeneralViewModel general_;
    AboutViewModel about_;
    bool refresh_pending_           = false;
    bool wifi_radio_sync_pending_   = false;
    bool wifi_sync_pending_         = false;
    bool bluetooth_sync_pending_    = false;
    bool published_foreground_busy_ = false;
};

}  // namespace lilygo::settings

#endif
