#include "pages/ethernet/ethernet_view_model.hpp"

#include <arpa/inet.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace lilygo::settings {
namespace {

constexpr auto ethernet_command_timeout = std::chrono::seconds(15);

bool foreground_command_busy(const SettingsCommandRunner *commands)
{
    return commands && commands->busy() && !settings_command_is_background(commands->active_command());
}

std::string trim_copy(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1U));
}

bool valid_ipv4(std::string_view value)
{
    const std::string candidate = trim_copy(value);
    if (candidate.empty() || candidate.size() >= INET_ADDRSTRLEN) return false;
    struct in_addr address{};
    return inet_pton(AF_INET, candidate.c_str(), &address) == 1;
}

std::optional<int> subnet_prefix(std::string_view value)
{
    const std::string candidate = trim_copy(value);
    if (candidate.empty()) return std::nullopt;
    if (candidate.front() == '/') {
        char *end = nullptr;
        const long prefix = std::strtol(candidate.c_str() + 1, &end, 10);
        if (!end || *end != '\0' || prefix < 0 || prefix > 32) return std::nullopt;
        return static_cast<int>(prefix);
    }
    struct in_addr mask{};
    if (inet_pton(AF_INET, candidate.c_str(), &mask) != 1) return std::nullopt;
    const std::uint32_t value_bits = ntohl(mask.s_addr);
    int prefix = 0;
    bool zero_seen = false;
    for (int bit = 31; bit >= 0; --bit) {
        if ((value_bits & (1U << bit)) != 0U) {
            if (zero_seen) return std::nullopt;
            ++prefix;
        } else {
            zero_seen = true;
        }
    }
    return prefix;
}

std::string dotted_mask(int prefix)
{
    const std::uint32_t mask = prefix == 0 ? 0U : 0xffffffffU << (32 - prefix);
    return std::to_string((mask >> 24) & 0xffU) + "." + std::to_string((mask >> 16) & 0xffU) + "." +
           std::to_string((mask >> 8) & 0xffU) + "." + std::to_string(mask & 0xffU);
}

bool valid_dns_list(std::string_view value)
{
    std::size_t offset = 0;
    while (offset <= value.size()) {
        const auto separator = value.find_first_of(",; \t\r\n", offset);
        const auto token = trim_copy(value.substr(offset, separator == std::string_view::npos
                                                          ? std::string_view::npos
                                                          : separator - offset));
        if (!token.empty() && !valid_ipv4(token)) return false;
        if (separator == std::string_view::npos) break;
        offset = separator + 1U;
    }
    return true;
}

std::string canonical_dns(std::string_view value)
{
    std::string result;
    std::size_t offset = 0;
    while (offset <= value.size()) {
        const auto separator = value.find_first_of(",; \t\r\n", offset);
        const auto token = trim_copy(value.substr(offset, separator == std::string_view::npos
                                                          ? std::string_view::npos
                                                          : separator - offset));
        if (!token.empty()) {
            if (!result.empty()) result.push_back(',');
            result += token;
        }
        if (separator == std::string_view::npos) break;
        offset = separator + 1U;
    }
    return result;
}

bool is_ethernet_command(SettingsCommand command)
{
    return command == SettingsCommand::ethernet_refresh || command == SettingsCommand::ethernet_profile_lookup ||
           command == SettingsCommand::ethernet_modify || command == SettingsCommand::ethernet_up;
}

std::string output_line(std::string_view output)
{
    const auto end = output.find_first_of("\r\n");
    return trim_copy(output.substr(0, end == std::string_view::npos ? output.size() : end));
}

std::string first_ethernet_profile(std::string_view output)
{
    std::size_t cursor = 0;
    while (cursor < output.size()) {
        const auto end = output.find('\n', cursor);
        const auto line = trim_copy(output.substr(cursor, end == std::string_view::npos ? std::string_view::npos
                                                                                           : end - cursor));
        const auto separator = line.rfind(':');
        if (separator != std::string::npos) {
            const auto type = trim_copy(std::string_view(line).substr(separator + 1U));
            if (type == "ethernet" || type == "802-3-ethernet") return trim_copy(std::string_view(line).substr(0, separator));
        }
        if (end == std::string_view::npos) break;
        cursor = end + 1U;
    }
    return {};
}

}  // namespace

EthernetViewModel::EthernetViewModel(SettingsModel &model) noexcept : model_(model)
{
}

EthernetViewModel::EthernetViewModel(SettingsModel &model, SettingsCommandRunner &commands) noexcept
    : model_(model), commands_(&commands)
{
}

void EthernetViewModel::publish()
{
    const bool busy = configuration_busy();
    presentation_.configurable = commands_ != nullptr && !foreground_command_busy(commands_) && model_.nmcli_available() &&
                                 !model_.ethernet_interfaces().empty();
    presentation_.configuration_busy = busy;
    presentation_.configuration_status = configuration_status_;
    presentation_.ipv4_dhcp = current_ipv4_dhcp_;
    if (model_.ethernet_interfaces().empty()) {
        presentation_.status = "No Ethernet Adapter";
        presentation_.interface_name = "--";
        presentation_.ipv4 = "--";
        presentation_.subnet = "--";
        presentation_.gateway = "--";
        presentation_.dns = "--";
        presentation_.mac = "--";
        presentation_.speed = "--";
        presentation_.duplex = "--";
        presentation_.mtu = "--";
        presentation_.ipv4_dhcp = true;
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

    const std::string ip = current_ipv4_.empty() ? interface.ipv4 : current_ipv4_;
    presentation_.status = status_override_.empty() ? (interface.connected ? "Connected" : "Not Connected")
                                                    : status_override_;
    presentation_.interface_name = interface.name;
    presentation_.ipv4 = ip.empty() ? "--" : ip;
    presentation_.subnet = current_subnet_.empty() ? "--" : current_subnet_;
    presentation_.gateway = current_gateway_.empty() ? "--" : current_gateway_;
    presentation_.dns = current_dns_.empty() ? "--" : current_dns_;
    presentation_.mac = interface.address;
    presentation_.speed = speed;
    presentation_.duplex = interface.duplex;
    presentation_.mtu = mtu;
    revision_.publish();
}

bool EthernetViewModel::sync()
{
    if (!commands_ || commands_->busy() || !model_.nmcli_available() || model_.ethernet_interfaces().empty()) {
        return false;
    }
    const auto &interface = model_.ethernet_interfaces().front();
    return start(SettingsCommand::ethernet_refresh,
                 {model_.environment().nmcli, "-t", "-f", "GENERAL.CONNECTION,IP4.ADDRESS,IP4.GATEWAY,IP4.DNS",
                  "device", "show", interface.name},
                 "Reading Ethernet Settings...");
}

bool EthernetViewModel::apply_dhcp_configuration(std::string_view dns)
{
    if (!commands_ || foreground_command_busy(commands_) || model_.ethernet_interfaces().empty() || !model_.nmcli_available()) {
        configuration_status_ = "Ethernet configuration unavailable";
        publish();
        return false;
    }
    const std::string dns_value = canonical_dns(dns);
    if (!valid_dns_list(dns_value) || dns_value.size() > 256U) {
        configuration_status_ = "Invalid Ethernet Settings";
        publish();
        return false;
    }
    pending_ipv4_.clear();
    pending_subnet_.clear();
    pending_gateway_.clear();
    pending_dns_ = dns_value;
    pending_ipv4_dhcp_ = true;
    connection_profile_.clear();
    configuration_mode_ = true;
    awaiting_profile_ = true;
    profile_list_attempted_ = false;
    configuration_status_.clear();
    const auto &interface = model_.ethernet_interfaces().front();
    if (!start(SettingsCommand::ethernet_profile_lookup,
               {model_.environment().nmcli, "-t", "-f", "GENERAL.CONNECTION", "device", "show", interface.name},
               "Finding Ethernet Profile...")) {
        awaiting_profile_ = false;
        return false;
    }
    return true;
}

void EthernetViewModel::begin_configuration() noexcept
{
    configuration_mode_ = true;
    configuration_status_.clear();
    publish();
}

bool EthernetViewModel::apply_configuration(std::string_view ipv4, std::string_view subnet,
                                             std::string_view gateway, std::string_view dns)
{
    if (!commands_ || foreground_command_busy(commands_) || model_.ethernet_interfaces().empty() || !model_.nmcli_available()) {
        configuration_status_ = !commands_ ? "Ethernet command runner unavailable"
                                            : commands_->busy() ? "Another Operation Is In Progress"
                                            : model_.ethernet_interfaces().empty() ? "No Ethernet Adapter"
                                            : "NetworkManager Is Unavailable";
        publish();
        return false;
    }
    const std::string ip_value = trim_copy(ipv4);
    const std::string mask_value = trim_copy(subnet);
    const std::string gateway_value = trim_copy(gateway);
    const std::string dns_value = canonical_dns(dns);
    const auto prefix = subnet_prefix(mask_value);
    if (!valid_ipv4(ip_value) || !prefix || !valid_dns_list(dns_value) ||
        (!gateway_value.empty() && !valid_ipv4(gateway_value)) || ip_value.size() > 64U || gateway_value.size() > 64U ||
        dns_value.size() > 256U) {
        configuration_status_ = "Invalid Ethernet Settings";
        publish();
        return false;
    }

    pending_ipv4_ = ip_value;
    pending_subnet_ = mask_value.front() == '/' ? dotted_mask(*prefix) : mask_value;
    pending_gateway_ = gateway_value;
    pending_dns_ = dns_value;
    pending_ipv4_dhcp_ = false;
    connection_profile_.clear();
    configuration_mode_ = true;
    awaiting_profile_ = true;
    profile_list_attempted_ = false;
    configuration_status_.clear();
    const auto &interface = model_.ethernet_interfaces().front();
    if (!start(SettingsCommand::ethernet_profile_lookup,
               {model_.environment().nmcli, "-t", "-f", "GENERAL.CONNECTION", "device", "show", interface.name},
               "Finding Ethernet Profile...")) {
        awaiting_profile_ = false;
        return false;
    }
    return true;
}

void EthernetViewModel::cancel_configuration() noexcept
{
    if (commands_ && commands_->busy() && is_ethernet_command(commands_->active_command())) commands_->stop();
    pending_ipv4_.clear();
    pending_subnet_.clear();
    pending_gateway_.clear();
    pending_dns_.clear();
    pending_ipv4_dhcp_ = false;
    connection_profile_.clear();
    awaiting_profile_ = false;
    profile_list_attempted_ = false;
    configuration_mode_ = false;
    configuration_status_.clear();
    status_override_.clear();
    publish();
}

void EthernetViewModel::command_finished(const SettingsCommandResult &result)
{
    if (result.command == SettingsCommand::ethernet_refresh) {
        status_override_.clear();
        if (!result.success) {
            configuration_status_ = "Cannot Read Ethernet Settings";
            publish();
            return;
        }
        current_ipv4_.clear();
        current_subnet_.clear();
        current_gateway_.clear();
        current_dns_.clear();
        std::string profile;
        std::string address;
        std::string gateway;
        std::string dns;
        std::size_t cursor = 0;
        while (cursor < result.output.size()) {
            const auto end = result.output.find('\n', cursor);
            const auto line = trim_copy(std::string_view(result.output).substr(
                cursor, end == std::string::npos ? std::string::npos : end - cursor));
            const auto separator = line.find(':');
            if (separator != std::string::npos) {
                const auto key = line.substr(0, separator);
                const auto value = trim_copy(std::string_view(line).substr(separator + 1U));
                if (key == "GENERAL.CONNECTION" && profile.empty()) profile = value;
                else if (key.rfind("IP4.ADDRESS", 0) == 0 && address.empty()) address = value;
                else if (key == "IP4.GATEWAY" && gateway.empty()) gateway = value;
                else if (key.rfind("IP4.DNS", 0) == 0 && value != "--") {
                    if (!dns.empty() && !value.empty()) dns.push_back(',');
                    dns += value;
                }
            }
            if (end == std::string::npos) break;
            cursor = end + 1U;
        }
        if (!address.empty()) {
            const auto slash = address.find('/');
            current_ipv4_ = trim_copy(std::string_view(address).substr(0, slash));
            if (slash != std::string::npos) {
                const auto prefix = subnet_prefix(address.substr(slash));
                if (prefix) current_subnet_ = dotted_mask(*prefix);
            }
        }
        if (!gateway.empty() && gateway != "--") current_gateway_ = gateway;
        if (!dns.empty() && dns != "--") current_dns_ = dns;
        if (!profile.empty() && profile != "--") connection_profile_ = profile;
        if (!connection_profile_.empty() && connection_profile_ != "--") {
            refresh_profile_pending_ = true;
            if (start(SettingsCommand::ethernet_profile_lookup,
                      {model_.environment().nmcli, "-t", "-f", "ipv4.method,ipv4.addresses,ipv4.gateway,ipv4.dns",
                       "connection", "show", connection_profile_},
                      "Reading Ethernet Configuration..."))
                return;
            refresh_profile_pending_ = false;
        }
        publish();
        return;
    }

    if (result.command == SettingsCommand::ethernet_profile_lookup) {
        awaiting_profile_ = false;
        if (!result.success) {
            configuration_status_ = "Cannot Find Ethernet Profile";
            publish();
            return;
        }
        if (refresh_profile_pending_) {
            refresh_profile_pending_ = false;
            std::size_t cursor = 0;
            while (cursor < result.output.size()) {
                const auto end = result.output.find('\n', cursor);
                const auto line = trim_copy(std::string_view(result.output).substr(
                    cursor, end == std::string::npos ? std::string::npos : end - cursor));
                const auto separator = line.find(':');
                if (separator != std::string::npos) {
                    const auto key = line.substr(0, separator);
                    const auto value = trim_copy(std::string_view(line).substr(separator + 1U));
                    if (key == "ipv4.method") current_ipv4_dhcp_ = value != "manual";
                    else if (key == "ipv4.gateway") current_gateway_ = value == "--" ? "" : value;
                    else if (key == "ipv4.dns") current_dns_ = value == "--" ? "" : value;
                    else if (key == "ipv4.addresses" && !value.empty() && value != "--") {
                        const auto slash = value.find('/');
                        current_ipv4_ = trim_copy(std::string_view(value).substr(0, slash));
                        if (slash != std::string::npos) {
                            const auto prefix = subnet_prefix(value.substr(slash));
                            if (prefix) current_subnet_ = dotted_mask(*prefix);
                        }
                    }
                }
                if (end == std::string::npos) break;
                cursor = end + 1U;
            }
            publish();
            return;
        }
        connection_profile_ = profile_list_attempted_ ? first_ethernet_profile(result.output) : output_line(result.output);
        constexpr std::string_view connection_prefix = "GENERAL.CONNECTION:";
        if (connection_profile_.rfind(connection_prefix, 0) == 0)
            connection_profile_.erase(0, connection_prefix.size());
        connection_profile_ = trim_copy(connection_profile_);
        if (connection_profile_.empty() || connection_profile_ == "--") {
            if (!profile_list_attempted_) {
                profile_list_attempted_ = true;
                if (start(SettingsCommand::ethernet_profile_lookup,
                           {model_.environment().nmcli, "-t", "-f", "NAME,TYPE", "connection", "show"},
                           "Finding Ethernet Profile..."))
                    return;
            }
            configuration_status_ = "Cannot Find Ethernet Profile";
            publish();
            return;
        }
        profile_list_attempted_ = false;
        const auto prefix = pending_ipv4_dhcp_ ? std::optional<int>(0) : subnet_prefix(pending_subnet_);
        if (!prefix) {
            configuration_status_ = "Invalid Ethernet Settings";
            publish();
            return;
        }
        std::vector<std::string> arguments = {model_.environment().nmcli, "connection", "modify", connection_profile_,
                                              "ipv4.method", pending_ipv4_dhcp_ ? "auto" : "manual",
                                              "ipv4.addresses", pending_ipv4_dhcp_ ? "" : pending_ipv4_ + "/" + std::to_string(*prefix),
                                              "ipv4.gateway", pending_gateway_, "ipv4.dns", pending_dns_};
        if (!start(SettingsCommand::ethernet_modify, std::move(arguments), "Saving Ethernet Settings...")) {
            configuration_status_ = "Cannot Save Ethernet Settings";
        }
        return;
    }

    if (result.command == SettingsCommand::ethernet_modify) {
        if (!result.success) {
            configuration_status_ = "Cannot Save Ethernet Settings";
            publish();
            return;
        }
        std::vector<std::string> arguments =
            {model_.environment().nmcli, "connection", "up", connection_profile_};
        if (!start(SettingsCommand::ethernet_up, std::move(arguments), "Applying Ethernet Settings...")) {
            configuration_status_ = "Cannot Apply Ethernet Settings";
        }
        return;
    }

    if (result.command == SettingsCommand::ethernet_up) {
        if (!result.success) {
            configuration_status_ = "Cannot Apply Ethernet Settings";
            publish();
            return;
        }
        current_ipv4_ = pending_ipv4_;
        current_subnet_ = pending_subnet_;
        current_gateway_ = pending_gateway_;
        current_dns_ = pending_dns_;
        current_ipv4_dhcp_ = pending_ipv4_dhcp_;
        model_.refresh_connectivity();
        status_override_ = "Connected";
        configuration_status_ = "Ethernet Settings Saved";
        configuration_mode_ = false;
        pending_ipv4_.clear();
        pending_subnet_.clear();
        pending_gateway_.clear();
        pending_dns_.clear();
        pending_ipv4_dhcp_ = false;
        publish();
    }
}

bool EthernetViewModel::configuration_busy() const noexcept
{
    return commands_ && commands_->busy() && is_ethernet_command(commands_->active_command());
}

const EthernetPresentation &EthernetViewModel::presentation() const noexcept
{
    return presentation_;
}

lv_subject_t *EthernetViewModel::revision_subject() noexcept
{
    return revision_.get();
}

bool EthernetViewModel::start(SettingsCommand command, std::vector<std::string> arguments, const char *progress)
{
    if (command != SettingsCommand::ethernet_refresh && commands_ && commands_->busy() &&
        settings_command_is_background(commands_->active_command()))
        commands_->stop();
    if (!commands_ || commands_->busy()) {
        configuration_status_ = "Another Operation Is In Progress";
        publish();
        return false;
    }

    // Read-only status queries can run as the current user. Profile changes
    // are wrapped in sudo -n so the UI never blocks on a password prompt.
    if (command == SettingsCommand::ethernet_modify || command == SettingsCommand::ethernet_up) {
        const auto &sudo = model_.environment().sudo;
        if (!sudo.empty()) arguments.insert(arguments.begin(), {sudo, "-n", "--"});
    }
    if (!commands_->start(command, arguments, {}, ethernet_command_timeout)) {
        configuration_status_ = "Cannot Start Ethernet Operation";
        publish();
        return false;
    }
    configuration_status_ = progress;
    publish();
    return true;
}

}  // namespace lilygo::settings
