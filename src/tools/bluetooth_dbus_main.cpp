#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct sd_bus;
struct sd_bus_message;

struct sd_bus_error {
    const char *name;
    const char *message;
    int need_free;
};

using sd_bus_open_system_fn = int (*)(sd_bus **);
using sd_bus_unref_fn       = sd_bus *(*)(sd_bus *);
using sd_bus_call_method_fn = int (*)(sd_bus *, const char *, const char *, const char *, const char *, sd_bus_error *,
                                      sd_bus_message **, const char *, ...);
using sd_bus_message_at_end_fn          = int (*)(sd_bus_message *, int);
using sd_bus_message_enter_container_fn = int (*)(sd_bus_message *, char, const char *);
using sd_bus_message_exit_container_fn  = int (*)(sd_bus_message *);
using sd_bus_message_peek_type_fn       = int (*)(sd_bus_message *, char *, const char **);
using sd_bus_message_read_fn            = int (*)(sd_bus_message *, const char *, ...);
using sd_bus_message_read_basic_fn      = int (*)(sd_bus_message *, char, void *);
using sd_bus_message_skip_fn            = int (*)(sd_bus_message *, const char *);
using sd_bus_message_unref_fn           = sd_bus_message *(*)(sd_bus_message *);
using sd_bus_error_free_fn              = sd_bus_error *(*)(sd_bus_error *);

struct SdBusApi {
    void *handle = nullptr;
    sd_bus_open_system_fn open_system = nullptr;
    sd_bus_unref_fn unref = nullptr;
    sd_bus_call_method_fn call_method = nullptr;
    sd_bus_message_at_end_fn at_end = nullptr;
    sd_bus_message_enter_container_fn enter_container = nullptr;
    sd_bus_message_exit_container_fn exit_container = nullptr;
    sd_bus_message_peek_type_fn peek_type = nullptr;
    sd_bus_message_read_fn read = nullptr;
    sd_bus_message_read_basic_fn read_basic = nullptr;
    sd_bus_message_skip_fn skip = nullptr;
    sd_bus_message_unref_fn message_unref = nullptr;
    sd_bus_error_free_fn error_free = nullptr;

    ~SdBusApi()
    {
        if (handle) dlclose(handle);
    }

    template <typename Function>
    bool load(Function &function, const char *name)
    {
        function = reinterpret_cast<Function>(dlsym(handle, name));
        return function != nullptr;
    }

    bool open()
    {
        handle = dlopen("libsystemd.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!handle) return false;
        return load(open_system, "sd_bus_open_system") && load(unref, "sd_bus_unref") &&
               load(call_method, "sd_bus_call_method") && load(at_end, "sd_bus_message_at_end") &&
               load(enter_container, "sd_bus_message_enter_container") &&
               load(exit_container, "sd_bus_message_exit_container") && load(peek_type, "sd_bus_message_peek_type") &&
               load(read, "sd_bus_message_read") && load(read_basic, "sd_bus_message_read_basic") &&
               load(skip, "sd_bus_message_skip") && load(message_unref, "sd_bus_message_unref") &&
               load(error_free, "sd_bus_error_free");
    }
};

struct Device {
    std::string address;
    std::string name;
    std::string alias;
    bool paired = false;
};

bool valid_address(const std::string &address)
{
    if (address.size() != 17U) return false;
    for (std::size_t index = 0; index < address.size(); ++index) {
        if ((index + 1U) % 3U == 0U) {
            if (address[index] != ':') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(address[index]))) {
            return false;
        }
    }
    return true;
}

bool same_address(const std::string &left, const std::string &right)
{
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index])))
            return false;
    }
    return true;
}

bool usable_name(const std::string &name, const std::string &address)
{
    auto looks_like_address = [](const std::string &value) {
        if (value.size() == 12U) {
            return std::all_of(value.begin(), value.end(), [](char character) {
                return std::isxdigit(static_cast<unsigned char>(character)) != 0;
            });
        }
        if (value.size() != 17U) return false;
        for (std::size_t index = 0; index < value.size(); ++index) {
            if ((index + 1U) % 3U == 0U) {
                if (value[index] != ':' && value[index] != '-') return false;
            } else if (!std::isxdigit(static_cast<unsigned char>(value[index]))) {
                return false;
            }
        }
        return true;
    };
    if (name.empty() || same_address(name, address) || looks_like_address(name)) return false;
    std::string normalized;
    normalized.reserve(name.size());
    for (const char character : name)
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    return normalized != "unknown" && normalized != "unknown device" && normalized != "(unknown)" &&
           normalized != "n/a" && normalized != "--";
}

bool at_end(const SdBusApi &api, sd_bus_message *message)
{
    const int result = api.at_end(message, 0);
    return result > 0;
}

bool read_string(const SdBusApi &api, sd_bus_message *message, const char *type, const char **value)
{
    return api.read(message, type, value) >= 0;
}

bool skip_variant_value(const SdBusApi &api, sd_bus_message *message)
{
    char type = 0;
    const char *contents = nullptr;
    if (api.peek_type(message, &type, &contents) <= 0) return false;
    if (type == 's') {
        const char *ignored = nullptr;
        return api.read_basic(message, 's', &ignored) >= 0;
    }
    return api.skip(message, nullptr) >= 0;
}

bool read_properties(const SdBusApi &api, sd_bus_message *message, Device &device)
{
    if (api.enter_container(message, 'a', "{sv}") <= 0) return false;
    while (!at_end(api, message)) {
        if (api.enter_container(message, 'e', "sv") <= 0) return false;
        const char *property = nullptr;
        if (!read_string(api, message, "s", &property)) return false;
        char variant_type = 0;
        const char *variant_contents = nullptr;
        if (api.peek_type(message, &variant_type, &variant_contents) <= 0 || variant_type != 'v' ||
            !variant_contents || api.enter_container(message, 'v', variant_contents) <= 0) {
            return false;
        }

        char type = 0;
        const char *contents = nullptr;
        if (api.peek_type(message, &type, &contents) <= 0) {
            return false;
        }
        const char *value = nullptr;
        if ((std::strcmp(property, "Address") == 0 || std::strcmp(property, "Name") == 0 ||
             std::strcmp(property, "Alias") == 0) && type == 's') {
            if (!read_string(api, message, "s", &value)) {
                return false;
            }
            if (std::strcmp(property, "Address") == 0)
                device.address = value ? value : "";
            else if (std::strcmp(property, "Alias") == 0 && value && value[0])
                device.alias = value;
            else if (std::strcmp(property, "Name") == 0 && value && value[0])
                device.name = value;
        } else if (std::strcmp(property, "Paired") == 0 && type == 'b') {
            int paired = 0;
            if (api.read_basic(message, 'b', &paired) < 0) return false;
            device.paired = paired != 0;
        } else if (!skip_variant_value(api, message)) {
            return false;
        }
        if (api.exit_container(message) < 0 || api.exit_container(message) < 0) {
            return false;
        }
    }
    return api.exit_container(message) >= 0;
}

bool read_interfaces(const SdBusApi &api, sd_bus_message *message, std::vector<Device> &devices)
{
    if (api.enter_container(message, 'a', "{sa{sv}}") <= 0) return false;
    while (!at_end(api, message)) {
        if (api.enter_container(message, 'e', "sa{sv}") <= 0) return false;
        const char *interface_name = nullptr;
        if (!read_string(api, message, "s", &interface_name)) return false;
        if (interface_name && std::strcmp(interface_name, "org.bluez.Device1") == 0) {
            Device device;
            if (!read_properties(api, message, device)) return false;
            if (valid_address(device.address)) {
                if (usable_name(device.alias, device.address))
                    device.name = device.alias;
                if (!usable_name(device.name, device.address)) device.name.clear();
                devices.push_back(std::move(device));
            }
        } else if (api.skip(message, nullptr) < 0) return false;
        if (api.exit_container(message) < 0) return false;
    }
    return api.exit_container(message) >= 0;
}

bool enumerate_devices(const SdBusApi &api, std::vector<Device> &devices)
{
    sd_bus *bus = nullptr;
    const int open_result = api.open_system(&bus);
    if (open_result < 0 || !bus) return false;
    sd_bus_error error{nullptr, nullptr, 0};
    sd_bus_message *reply = nullptr;
    const int call_result = api.call_method(bus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
                                            "GetManagedObjects", &error, &reply, "");
    if (call_result < 0 || !reply) {
        if (error.name || error.message) api.error_free(&error);
        api.unref(bus);
        return false;
    }

    bool success = api.enter_container(reply, 'a', "{oa{sa{sv}}}") > 0;
    while (success && !at_end(api, reply)) {
        if (api.enter_container(reply, 'e', "oa{sa{sv}}") <= 0) {
            success = false;
            break;
        }
        const char *object_path = nullptr;
        if (!read_string(api, reply, "o", &object_path) || !read_interfaces(api, reply, devices) ||
            api.exit_container(reply) < 0) {
            success = false;
            break;
        }
    }
    if (success && api.exit_container(reply) < 0) success = false;
    api.message_unref(reply);
    if (error.name || error.message) api.error_free(&error);
    api.unref(bus);
    if (!success) return false;

    std::sort(devices.begin(), devices.end(), [](const Device &left, const Device &right) {
        return left.address < right.address;
    });
    devices.erase(std::unique(devices.begin(), devices.end(), [](const Device &left, const Device &right) {
                       return same_address(left.address, right.address);
                   }),
                  devices.end());
    return true;
}

void print_devices(const std::vector<Device> &devices)
{
    for (const auto &device : devices) {
        std::printf("Device %s", device.address.c_str());
        if (!device.name.empty()) std::putchar(' ');
        for (const char character : device.name) {
            if (character == '\n' || character == '\r' || character == '\t')
                std::putchar(' ');
            else
                std::putchar(character);
        }
        std::printf("\tPaired: %s\n", device.paired ? "yes" : "no");
    }
}

}  // namespace

int main()
{
    SdBusApi api;
    if (!api.open()) return 2;
    std::vector<Device> devices;
    if (!enumerate_devices(api, devices)) return 3;
    print_devices(devices);
    return 0;
}
