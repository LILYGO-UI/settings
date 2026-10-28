#include "pages/battery/battery_model.hpp"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <string_view>

int main(int argc, char **argv)
{
    // This executable is a narrow privilege boundary. Never accept a caller's
    // sysfs path, environment override, arbitrary attribute, or shell command.
    if (argc < 4) return 2;
    const std::string_view action(argv[1]);
    const bool capacity = action == "capacity";
    if ((capacity && argc != 5) || (!capacity && argc != 4)) return 2;
    if (!capacity && action != "input-current" && action != "charge-current") return 2;
    const auto parse = [](const char *argument, std::int64_t &value) {
        const std::string_view text(argument);
        if (text.empty() || text.size() > 10 || text.find_first_not_of("0123456789") != std::string_view::npos)
            return false;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    };
    std::int64_t value = 0, full = 0;
    if (!parse(argv[2], value) ||
        (capacity && (!parse(argv[3], full) || value < 1 || value > 32767 || full < 1 || full > 32767)))
        return 2;

    const std::string_view token(argv[argc - 1]);
    const auto colon = token.find(':');
    if (token.size() > 41 || colon == std::string_view::npos || colon == 0 || colon + 1 == token.size() ||
        token.find(':', colon + 1) != std::string_view::npos ||
        token.find_first_not_of("0123456789:") != std::string_view::npos)
        return 2;

    lilygo::settings::BatteryModel model("/sys/class/power_supply");
    model.refresh();
    const auto result =
        capacity ? model.set_capacity_configuration(static_cast<int>(value), static_cast<int>(full), std::string(token))
        : action == "input-current" ? model.set_input_current_limit(value, std::string(token))
                                    : model.set_charge_current(value, std::string(token));
    std::fprintf(result.success ? stdout : stderr, "%s\n", result.message.c_str());
    return result.success ? 0 : 1;
}
