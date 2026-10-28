#ifndef LILYGO_UI_SETTINGS_PAGES_WIFI_WIFI_MODEL_HPP
#define LILYGO_UI_SETTINGS_PAGES_WIFI_WIFI_MODEL_HPP

#include "domain/settings_model.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lilygo::settings {
struct WifiKnownNetwork {
    std::string uuid;
    std::string ssid;
    std::string security;
    bool in_range  = false;
    bool connected = false;
};
struct WifiProfileReference {
    std::string uuid;
    std::string interface;
    bool active = false;
};
struct WifiConnectionDetails {
    std::string ssid, profile_uuid, status;
    std::string ipv4_method, ipv4_address, subnet_mask, ipv4_gateway;
    std::string ipv6_method, ipv6_address, ipv6_gateway;
    std::string dns_servers, search_domains, interface, mac, mtu;
};
class WifiModel {
public:
    [[nodiscard]] static bool valid_uuid(std::string_view uuid) noexcept;
    [[nodiscard]] static std::optional<std::vector<WifiProfileReference>> parse_profiles(std::string_view output);
    [[nodiscard]] static std::optional<WifiKnownNetwork> parse_profile(std::string_view output, std::string_view uuid);
    [[nodiscard]] static std::optional<WifiConnectionDetails> parse_device(std::string_view output,
                                                                           std::string_view interface);
    [[nodiscard]] static bool apply_details_profile(WifiConnectionDetails &details, std::string_view output);
    void replace_known(std::vector<WifiKnownNetwork> networks);
    void update_reachability(const std::vector<WifiNetwork> &networks, bool enabled);
    void remove_known(std::string_view uuid);
    [[nodiscard]] const std::vector<WifiKnownNetwork> &known_networks() const noexcept
    {
        return known_;
    }
    [[nodiscard]] std::size_t generation() const noexcept
    {
        return generation_;
    }

private:
    std::vector<WifiKnownNetwork> known_;
    std::size_t generation_ = 0;
};
}  // namespace lilygo::settings
#endif
