#include "pages/wifi/wifi_model.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>

namespace lilygo::settings {
namespace {
std::vector<std::string> fields(std::string_view line)
{
    std::vector<std::string> result(1);
    bool escaped = false;
    for (char c : line) {
        if (escaped) {
            result.back().push_back(c);
            escaped = false;
        } else if (c == '\\')
            escaped = true;
        else if (c == ':')
            result.emplace_back();
        else
            result.back().push_back(c);
    }
    if (escaped) result.back().push_back('\\');
    return result;
}
std::vector<std::string_view> lines(std::string_view output)
{
    std::vector<std::string_view> result;
    while (!output.empty()) {
        const auto end = output.find('\n');
        auto line      = output.substr(0, end);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty()) result.push_back(line);
        if (end == std::string_view::npos) break;
        output.remove_prefix(end + 1);
    }
    return result;
}
using Properties = std::map<std::string, std::string>;
Properties properties(std::string_view output)
{
    Properties result;
    for (const auto line : lines(output)) {
        const auto columns = fields(line);
        if (columns.size() < 2) continue;
        std::string value = columns[1];
        // IPv6 and hardware values may be unescaped on older nmcli versions.
        for (std::size_t i = 2; i < columns.size(); ++i) value += ':' + columns[i];
        result[columns[0]] = value == "--" && columns[0] != "802-11-wireless.ssid" ? "" : value;
    }
    return result;
}
void append(std::string &target, const std::string &value)
{
    if (value.empty()) return;
    if (!target.empty()) target += '\n';
    target += value;
}
bool field_is(const std::string &key, std::string_view prefix)
{
    return key == prefix ||
           (key.size() > prefix.size() && key.compare(0, prefix.size(), prefix) == 0 && key[prefix.size()] == '[');
}
std::string method(const std::string &value, bool ipv4)
{
    if (value == "auto") return ipv4 ? "Using DHCP" : "Automatic";
    if (value == "manual") return "Manual";
    if (value == "disabled" || value == "ignore") return "Disabled";
    if (value == "link-local") return "Link-Local";
    if (value == "shared") return "Shared";
    return value;
}
bool equal(const WifiKnownNetwork &a, const WifiKnownNetwork &b)
{
    return a.uuid == b.uuid && a.ssid == b.ssid && a.security == b.security && a.in_range == b.in_range &&
           a.connected == b.connected;
}
}  // namespace

bool WifiModel::valid_uuid(std::string_view uuid) noexcept
{
    if (uuid.size() != 36) return false;
    for (std::size_t i = 0; i < uuid.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (uuid[i] != '-') return false;
        } else if (!((uuid[i] >= '0' && uuid[i] <= '9') || (uuid[i] >= 'a' && uuid[i] <= 'f') ||
                     (uuid[i] >= 'A' && uuid[i] <= 'F')))
            return false;
    }
    return true;
}
std::optional<std::vector<WifiProfileReference>> WifiModel::parse_profiles(std::string_view output)
{
    std::vector<WifiProfileReference> result;
    for (const auto line : lines(output)) {
        const auto value = fields(line);
        if (value.size() != 4 || !valid_uuid(value[0])) return std::nullopt;
        if (value[1] != "802-11-wireless" && value[1] != "wifi") continue;
        if (std::any_of(result.begin(), result.end(), [&](const auto &entry) { return entry.uuid == value[0]; }))
            continue;
        result.push_back({value[0], value[3] == "--" ? "" : value[3], value[2] == "yes"});
    }
    return result;
}
std::optional<WifiKnownNetwork> WifiModel::parse_profile(std::string_view output, std::string_view uuid)
{
    const auto values = properties(output);
    const auto id     = values.find("connection.uuid");
    const auto ssid   = values.find("802-11-wireless.ssid");
    if (!valid_uuid(uuid) || id == values.end() || id->second != uuid || ssid == values.end() || ssid->second.empty())
        return std::nullopt;
    const auto type = values.find("connection.type");
    if (type == values.end() || (type->second != "802-11-wireless" && type->second != "wifi")) return std::nullopt;
    const auto key       = values.find("802-11-wireless-security.key-mgmt");
    std::string security = "Open";
    if (key != values.end() && !key->second.empty()) {
        if (key->second == "wpa-psk")
            security = "WPA/WPA2";
        else if (key->second == "sae")
            security = "WPA3";
        else if (key->second == "wpa-eap" || key->second == "wpa-eap-suite-b-192")
            security = "Enterprise";
        else
            security = "Secured";
    }
    return WifiKnownNetwork{std::string(uuid), ssid->second, security, false, false};
}
std::optional<WifiConnectionDetails> WifiModel::parse_device(std::string_view output, std::string_view interface)
{
    auto values = properties(output);
    if (values["GENERAL.DEVICE"] != interface || values["GENERAL.STATE"].rfind("100", 0) != 0 ||
        !valid_uuid(values["GENERAL.CON-UUID"]))
        return std::nullopt;
    WifiConnectionDetails result;
    result.interface    = interface;
    result.profile_uuid = values["GENERAL.CON-UUID"];
    result.mac          = values["GENERAL.HWADDR"];
    result.mtu          = values["GENERAL.MTU"];
    result.status       = "Connected";
    result.ipv4_gateway = values["IP4.GATEWAY"];
    result.ipv6_gateway = values["IP6.GATEWAY"];
    for (const auto &[key, value] : values) {
        if (field_is(key, "IP4.ADDRESS")) {
            const auto slash = value.find('/');
            append(result.ipv4_address, value.substr(0, slash));
            if (slash != std::string::npos) {
                int prefix        = -1;
                const auto parsed = std::from_chars(value.data() + slash + 1, value.data() + value.size(), prefix);
                if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && prefix >= 0 &&
                    prefix <= 32) {
                    const std::uint32_t mask = prefix == 0 ? 0U : 0xffffffffU << (32 - prefix);
                    append(result.subnet_mask,
                           std::to_string((mask >> 24) & 255) + "." + std::to_string((mask >> 16) & 255) + "." +
                               std::to_string((mask >> 8) & 255) + "." + std::to_string(mask & 255));
                }
            }
        } else if (field_is(key, "IP6.ADDRESS"))
            append(result.ipv6_address, value);
        else if (field_is(key, "IP4.DNS") || field_is(key, "IP6.DNS"))
            append(result.dns_servers, value);
        else if (field_is(key, "IP4.DOMAIN") || field_is(key, "IP6.DOMAIN"))
            append(result.search_domains, value);
    }
    return result;
}
bool WifiModel::apply_details_profile(WifiConnectionDetails &details, std::string_view output)
{
    const auto profile = parse_profile(output, details.profile_uuid);
    if (!profile) return false;
    auto values = properties(output);
    if (!values.count("ipv4.method") || !values.count("ipv6.method")) return false;
    details.ssid        = profile->ssid;
    details.ipv4_method = method(values["ipv4.method"], true);
    details.ipv6_method = method(values["ipv6.method"], false);
    return true;
}
void WifiModel::replace_known(std::vector<WifiKnownNetwork> networks)
{
    std::sort(networks.begin(), networks.end(),
              [](const auto &a, const auto &b) { return a.ssid == b.ssid ? a.uuid < b.uuid : a.ssid < b.ssid; });
    if (known_.size() == networks.size() && std::equal(known_.begin(), known_.end(), networks.begin(), equal)) return;
    known_ = std::move(networks);
    ++generation_;
}
void WifiModel::update_reachability(const std::vector<WifiNetwork> &networks, bool enabled)
{
    bool changed = false;
    for (auto &known : known_) {
        const bool in_range =
            enabled && std::any_of(networks.begin(), networks.end(), [&](const auto &network) {
                return network.ssid == known.ssid && ((network.security == "Open") == (known.security == "Open"));
            });
        if (known.in_range != in_range) {
            known.in_range = in_range;
            changed        = true;
        }
        if (known.connected && (!enabled || std::none_of(networks.begin(), networks.end(), [&](const auto &network) {
                return network.ssid == known.ssid && network.connected;
            }))) {
            known.connected = false;
            changed         = true;
        }
    }
    if (changed) ++generation_;
}
void WifiModel::remove_known(std::string_view uuid)
{
    const auto old_size = known_.size();
    known_.erase(std::remove_if(known_.begin(), known_.end(), [&](const auto &entry) { return entry.uuid == uuid; }),
                 known_.end());
    if (known_.size() != old_size) ++generation_;
}
}  // namespace lilygo::settings
