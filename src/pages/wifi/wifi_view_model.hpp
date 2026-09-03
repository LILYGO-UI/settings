#ifndef LILYGO_UI_SETTINGS_PAGES_WIFI_WIFI_VIEW_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_WIFI_WIFI_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/settings_command_runner.hpp"
#include "domain/settings_model.hpp"

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
};

class WifiViewModel {
public:
    WifiViewModel(SettingsModel &model, SettingsCommandRunner &commands) noexcept;

    void publish();
    void refresh();
    [[nodiscard]] bool sync_radio();
    [[nodiscard]] bool sync();
    void scan();
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
