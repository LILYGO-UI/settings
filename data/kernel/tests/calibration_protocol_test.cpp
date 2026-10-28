#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <vector>
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
#define BIT(n) (1U << (n))
struct attribute {};
struct device_attribute {
    attribute attr;
};
struct attribute_group {
    attribute **attrs;
};
#define DEVICE_ATTR_RW(name) device_attribute dev_attr_##name
struct device {
    void *data;
    device *parent;
};
struct power_supply {
    void *data;
};
struct bq27xxx_device_info;
struct bq_bus {
    int (*read)(bq27xxx_device_info *, u8, bool);
    int (*write)(bq27xxx_device_info *, u8, int, bool);
    int (*read_bulk)(bq27xxx_device_info *, u8, u8 *, int);
    int (*write_bulk)(bq27xxx_device_info *, u8, u8 *, int);
};
struct bq27xxx_device_info {
    bq_bus bus;
    device *dev;
    int lock;
    int charge_design_full;
    unsigned long last_update;
};
static unsigned long jiffies = 1000;
#define HZ 100
static void usleep_range(int, int)
{
}
static void msleep(int)
{
}
static void mutex_lock(int *lock)
{
    assert(!*lock);
    *lock = 1;
}
static bool mutex_trylock(int *lock)
{
    if (*lock) return false;
    *lock = 1;
    return true;
}
static void mutex_unlock(int *lock)
{
    assert(*lock);
    *lock = 0;
}
static void power_supply_changed(power_supply *)
{
}
#define sysfs_emit(buf, ...) snprintf(buf, 4096, __VA_ARGS__)
#define dev_err(dev, ...)    ((void)(dev))
static int kstrtouint(char *s, int base, unsigned int *out)
{
    if (*s == '-') return -EINVAL;
    errno = 0;
    char *end;
    unsigned long long n = strtoull(s, &end, base);
    if (!*s || *end || errno || n > UINT32_MAX) return -EINVAL;
    *out = static_cast<unsigned int>(n);
    return 0;
}
static u32 adapter_clock        = 100000;
static bool adapter_clock_known = true;
static int device_property_read_u32(device *, const char *, u32 *out)
{
    if (!adapter_clock_known) return -EINVAL;
    *out = adapter_clock;
    return 0;
}
// Kernel C implicit void conversions are made explicit for this C++17 harness.
#define dev_get_drvdata(dev)          static_cast<power_supply *>((dev)->data)
#define power_supply_get_drvdata(psy) static_cast<bq27xxx_device_info *>((psy)->data)
#include "bq27220-calibration.c"

struct MockGauge {
    u8 dm[65536]{};
    u8 pending[32]{};
    int security             = 6;
    int flags                = 0;
    int addr                 = 0;
    int design               = 3000;
    int full                 = 3000;
    int writes               = 0;
    int key_count            = 0;
    int commit_count         = 0;
    bool custom_key          = false;
    bool fail_commit_once    = false;
    bool wrong_readback_once = false;
    std::vector<int> commands;
    void be(int a, int v)
    {
        dm[a]     = v >> 8;
        dm[a + 1] = v;
    }
    int word(int a)
    {
        return (dm[a] << 8) | dm[a + 1];
    }
    void init(int sec = 6, int reserve = 0)
    {
        *this    = MockGauge{};
        security = sec;
        for (int i = 0; i < 32; i++) dm[0x929d + i] = static_cast<u8>(i + 20);
        be(0x929d, 3000 + reserve);
        be(0x929f, 3000);
        be(0x926d, reserve);
    }
} gauge;
static int read_reg(bq27xxx_device_info *, u8 reg, bool)
{
    switch (reg) {
        case 0x3a:
            return gauge.security | gauge.flags;
        case 0x3c:
            return gauge.design;
        case 0x12:
            if (gauge.wrong_readback_once && gauge.commit_count) {
                gauge.wrong_readback_once = false;
                return 99;
            }
            return gauge.full;
        case 0x3e:
            return gauge.addr;
        case 0x60: {
            u8 sum = (gauge.addr & 255) + (gauge.addr >> 8);
            for (int i = 0; i < 32; i++) sum += gauge.dm[gauge.addr + i];
            return (36 << 8) | (255 - sum);
        }
        default:
            return -EINVAL;
    }
}
static int write_reg(bq27xxx_device_info *, u8 reg, int value, bool)
{
    gauge.writes++;
    if (reg == 0) {
        gauge.commands.push_back(value);
        if (value == 0x8000 || value == 0xffff) {
            if (++gauge.key_count == 2) {
                gauge.key_count = 0;
                if (value == 0x8000 && !gauge.custom_key) gauge.security = 4;
                if (value == 0xffff && gauge.security == 4) gauge.security = 2;
            }
        } else if (value == 0x30)
            gauge.security = 6;
        else if (value == 0x90) {
            if (gauge.security != 2) return -EACCES;
            gauge.flags |= BIT(10);
        } else if (value == 0x91 || value == 0x92) {
            gauge.flags &= ~BIT(10);
            if (value == 0x91) {
                gauge.design = gauge.word(0x929f);
                gauge.full   = gauge.word(0x929d) - gauge.word(0x926d);
            }
        }
        return 0;
    }
    if (gauge.security != 2) return -EACCES;
    if (reg == 0x3e) {
        gauge.addr = value;
        return 0;
    }
    if (reg == 0x60) {
        u8 sum = (gauge.addr & 255) + (gauge.addr >> 8);
        for (int i = 0; i < 32; i++) sum += gauge.pending[i];
        assert((value >> 8) == 36 && (value & 255) == 255 - sum);
        assert(gauge.flags & BIT(10));
        memcpy(gauge.dm + gauge.addr, gauge.pending, 32);
        gauge.commit_count++;
        if (gauge.fail_commit_once) {
            gauge.fail_commit_once = false;
            return -EIO;
        }
        return 0;
    }
    return -EINVAL;
}
static int read_bulk(bq27xxx_device_info *, u8 reg, u8 *data, int length)
{
    assert(reg == 0x40 && length == 32);
    memcpy(data, gauge.dm + gauge.addr, length);
    return 0;
}
static int write_bulk(bq27xxx_device_info *, u8 reg, u8 *data, int length)
{
    assert(reg == 0x40 && length == 32);
    memcpy(gauge.pending, data, length);
    return 0;
}
int main()
{
    device dev{};
    power_supply psy{};
    bq27xxx_device_info di{{read_reg, write_reg, read_bulk, write_bulk}, &dev, 0, 0, 0};
    dev.data         = &psy;
    dev.parent       = &dev;
    psy.data         = &di;
    const auto store = [&](const std::string &s) {
        return calibration_capacity_store(&dev, nullptr, s.data(), s.size());
    };
    for (int sec : {2, 4, 6}) {
        gauge.init(sec, 200);
        u8 preserved[28];
        memcpy(preserved, gauge.dm + 0x92a1, 28);
        assert(store("2800 3100\n") == 10);
        assert(gauge.design == 2800 && gauge.full == 3100 && gauge.word(0x929d) == 3300);
        assert(!memcmp(preserved, gauge.dm + 0x92a1, 28));
        assert(gauge.security == sec && !(gauge.flags & BIT(10)));
    }
    for (const std::string &s : std::vector<std::string>{"", "0 1", "1 0", "32768 1", "1 32768", "-1 1", "1 2 3",
                                                         "4294967297 1", "1", "1 2bad", std::string("1 2\0x", 5)}) {
        gauge.init();
        assert(store(s) < 0 && gauge.writes == 0);
    }
    for (int flags : {int(BIT(10)), int(BIT(0)), int(BIT(4))}) {
        gauge.init();
        gauge.flags = flags;
        assert(store("1000 1200") == -EBUSY && gauge.writes == 0);
    }
    gauge.init();
    gauge.custom_key = true;
    assert(store("1000 1200") == -EACCES && gauge.commit_count == 0 && gauge.security == 6);
    gauge.init(6, 200);
    assert(store("1000 32767") == -ERANGE && gauge.commit_count == 0 && gauge.security == 6 && gauge.flags == 0);
    for (bool fail_bus : {false, true}) {
        gauge.init(6, 200);
        gauge.fail_commit_once    = fail_bus;
        gauge.wrong_readback_once = !fail_bus;
        assert(store("4000 4200") < 0);
        assert(gauge.word(0x929d) == 3200 && gauge.word(0x929f) == 3000 && gauge.security == 6 && gauge.flags == 0);
    }
    gauge.init();
    char out[4096];
    di.lock = 1;
    assert(calibration_capacity_show(&dev, nullptr, out) == -EBUSY);
    assert(gauge.writes == 0 && di.lock == 1);
    di.lock = 0;
    assert(calibration_capacity_show(&dev, nullptr, out) > 0);
    assert(!strcmp(out, "3000 3000\n") && gauge.writes == 0);
    for (u32 clock : {0U, 400000U}) {
        gauge.init();
        adapter_clock = clock;
        assert(store("1000 1200") == -EOPNOTSUPP && gauge.writes == 0);
    }
    adapter_clock       = 100000;
    adapter_clock_known = false;
    gauge.init();
    assert(store("1000 1200") == -EOPNOTSUPP && gauge.writes == 0);
    puts(
        "BQ27220 mock protocol: success, security states, reserve, parser, busy, custom key, rollback and read-only "
        "show passed");
}
