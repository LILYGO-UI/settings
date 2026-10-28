#ifndef LILYGO_UI_SETTINGS_PAGES_WIFI_WIFI_VIEW_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_WIFI_WIFI_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/settings_command_runner.hpp"
#include "domain/settings_model.hpp"
#include "pages/wifi/wifi_model.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace lilygo::settings {

enum class WifiActionResult { ignored, started, password_required };

struct WifiPresentation {
    bool available       = false;
    bool enabled         = false;
    bool control_enabled = false;
    std::string status;
    std::size_t list_generation = 0;
    std::string known_status;
    std::size_t known_generation = 0;
    bool details_open            = false;
    bool details_loading         = false;
    bool details_available       = false;
    std::string details_status;
};

class WifiViewModel {
public:
    WifiViewModel(SettingsModel &model, SettingsCommandRunner &commands) noexcept;

    void publish();
    void refresh();
    [[nodiscard]] bool sync_radio();
    [[nodiscard]] bool sync();
    void scan();
    void open_details();
    void close_details();
    void stop() noexcept;
    [[nodiscard]] WifiActionResult activate_known_network(std::size_t index);
    [[nodiscard]] bool forget_network(std::string_view uuid);
    [[nodiscard]] bool resume_background();
    void request_profiles_refresh() noexcept;
    [[nodiscard]] const std::vector<WifiKnownNetwork> &known_networks() const noexcept;
    [[nodiscard]] const WifiConnectionDetails &details() const noexcept;
    void set_enabled(bool enabled);
    [[nodiscard]] WifiActionResult activate_network(std::size_t index);
    [[nodiscard]] bool connect_pending_network(const char *password);
    void cancel_pending_network() noexcept;
    void command_finished(const SettingsCommandResult &result);

    [[nodiscard]] const WifiPresentation &presentation() const noexcept;
    [[nodiscard]] const std::vector<WifiNetwork> &networks() const noexcept;
    [[nodiscard]] const std::string &pending_ssid() const noexcept;
    [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
    [[nodiscard]] bool start(SettingsCommand command, std::vector<std::string> arguments, const char *progress,
                             std::string_view standard_input = {});
    void update_list_generation() noexcept;
    [[nodiscard]] std::string derived_status() const;

    void invalidate_details(const char *status);
    [[nodiscard]] bool start_profile_query();
    [[nodiscard]] bool details_connection_valid() const;
    WifiModel wifi_model_;
    WifiConnectionDetails details_;
    WifiConnectionDetails pending_details_;
    std::vector<WifiProfileReference> profiles_;
    std::vector<WifiKnownNetwork> pending_known_;
    std::size_t profile_index_     = 0;
    bool profiles_refresh_pending_ = false;
    bool profile_query_pending_    = false;
    bool profiles_partial_failure_ = false;
    bool details_query_pending_    = false;
    bool details_profile_pending_  = false;
    bool details_verifying_        = false;
    std::string details_ssid_;
    std::string details_interface_;
    std::string forget_uuid_;
    SettingsModel &model_;
    SettingsCommandRunner &commands_;
    WifiPresentation presentation_;
    std::string status_override_;
    std::string pending_ssid_;
    std::size_t list_generation_           = 0;
    std::size_t observed_model_generation_ = 0;
    bool observed_list_state_valid_        = false;
    bool observed_available_               = false;
    bool observed_enabled_                 = false;
    components::RevisionSubject revision_;
};

}  // namespace lilygo::settings

#endif
