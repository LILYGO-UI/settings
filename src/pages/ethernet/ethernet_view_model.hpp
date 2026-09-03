#ifndef LILYGO_UI_SETTINGS_PAGES_ETHERNET_ETHERNET_VIEW_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_ETHERNET_ETHERNET_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/settings_model.hpp"

#include <string>

namespace lilygo::settings {

struct EthernetPresentation {
    std::string status;
    std::string interface_name;
    std::string ipv4;
    std::string mac;
    std::string speed;
    std::string duplex;
    std::string mtu;
};

class EthernetViewModel {
public:
    explicit EthernetViewModel(SettingsModel &model) noexcept;
    void publish();
    [[nodiscard]] const EthernetPresentation &presentation() const noexcept;
    [[nodiscard]] lv_subject_t *revision_subject() noexcept;

private:
    SettingsModel &model_;
    EthernetPresentation presentation_;
    components::RevisionSubject revision_;
};

}  // namespace lilygo::settings

#endif
