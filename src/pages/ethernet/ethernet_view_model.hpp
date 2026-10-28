#ifndef LILYGO_UI_SETTINGS_PAGES_ETHERNET_ETHERNET_VIEW_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_ETHERNET_ETHERNET_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/settings_command_runner.hpp"
#include "domain/settings_model.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace lilygo::settings {

struct EthernetPresentation {
    std::string status;
    std::string interface_name;
    std::string ipv4;
    std::string subnet;
    std::string gateway;
    std::string dns;
    std::string mac;
    std::string speed;
    std::string duplex;
    std::string mtu;
    bool ipv4_dhcp = true;
    bool configurable          = false;
    bool configuration_busy    = false;
    std::string configuration_status;
};

class EthernetViewModel {
public:
    explicit EthernetViewModel(SettingsModel &model) noexcept;
    EthernetViewModel(SettingsModel &model, SettingsCommandRunner &commands) noexcept;

    void publish();
    [[nodiscard]] bool sync();
    void begin_configuration() noexcept;
    [[nodiscard]] bool apply_configuration(std::string_view ipv4, std::string_view subnet,
                                            std::string_view gateway, std::string_view dns);
    [[nodiscard]] bool apply_dhcp_configuration(std::string_view dns);
    void cancel_configuration() noexcept;
    void command_finished(const SettingsCommandResult &result);
    [[nodiscard]] bool configuration_busy() const noexcept;

    [[nodiscard]] const EthernetPresentation &presentation() const noexcept;
    [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
    [[nodiscard]] bool start(SettingsCommand command, std::vector<std::string> arguments,
                             const char *progress);

    SettingsModel &model_;
    SettingsCommandRunner *commands_ = nullptr;
    EthernetPresentation presentation_;
    std::string status_override_;
    std::string configuration_status_;
    std::string pending_ipv4_;
    std::string pending_subnet_;
    std::string pending_gateway_;
    std::string pending_dns_;
    bool pending_ipv4_dhcp_ = false;
    std::string connection_profile_;
    std::string current_ipv4_;
    std::string current_subnet_;
    std::string current_gateway_;
    std::string current_dns_;
    bool configuration_mode_ = false;
    bool awaiting_profile_   = false;
    bool profile_list_attempted_ = false;
    bool refresh_profile_pending_ = false;
    bool current_ipv4_dhcp_ = true;
    components::RevisionSubject revision_;
};

}  // namespace lilygo::settings

#endif
