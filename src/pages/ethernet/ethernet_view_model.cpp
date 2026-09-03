#include "pages/ethernet/ethernet_view_model.hpp"

#include <cstdio>

namespace lilygo::settings {

EthernetViewModel::EthernetViewModel(SettingsModel &model) noexcept : model_(model)
{
}

void EthernetViewModel::publish()
{
    if (model_.ethernet_interfaces().empty()) {
        presentation_ = {"No Ethernet Adapter", "--", "--", "--", "--", "--", "--"};
        revision_.publish();
        return;
    }

    const auto &interface = model_.ethernet_interfaces().front();
    char speed[64];
    char mtu[64];
    if (interface.speed_mbps > 0)
        std::snprintf(speed, sizeof(speed), "%d Mbps", interface.speed_mbps);
    else
        std::snprintf(speed, sizeof(speed), "--");
    if (interface.mtu > 0)
        std::snprintf(mtu, sizeof(mtu), "%d", interface.mtu);
    else
        std::snprintf(mtu, sizeof(mtu), "--");
    presentation_ = {
        interface.connected ? "Connected" : "Not Connected",
        interface.name,
        interface.ipv4,
        interface.address,
        speed,
        interface.duplex,
        mtu,
    };
    revision_.publish();
}

const EthernetPresentation &EthernetViewModel::presentation() const noexcept
{
    return presentation_;
}

lv_subject_t *EthernetViewModel::revision_subject() noexcept
{
    return revision_.get();
}

}  // namespace lilygo::settings
