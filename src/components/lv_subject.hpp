#ifndef LILYGO_UI_SETTINGS_COMPONENTS_LV_SUBJECT_HPP
#define LILYGO_UI_SETTINGS_COMPONENTS_LV_SUBJECT_HPP

#include <lvgl.h>

#include <cstdint>
#include <limits>

namespace lilygo::settings::components {

class IntSubject {
public:
    explicit IntSubject(std::int32_t value, std::int32_t minimum = std::numeric_limits<std::int32_t>::min(),
                        std::int32_t maximum = std::numeric_limits<std::int32_t>::max())
    {
        lv_subject_init_int(&subject_, value);
        lv_subject_set_min_value_int(&subject_, minimum);
        lv_subject_set_max_value_int(&subject_, maximum);
    }

    ~IntSubject()
    {
        lv_subject_deinit(&subject_);
    }

    IntSubject(const IntSubject &)            = delete;
    IntSubject &operator=(const IntSubject &) = delete;
    IntSubject(IntSubject &&)                 = delete;
    IntSubject &operator=(IntSubject &&)      = delete;

    void set(std::int32_t value)
    {
        lv_subject_set_int(&subject_, value);
    }
    [[nodiscard]] std::int32_t value() const
    {
        return lv_subject_get_int(const_cast<lv_subject_t *>(&subject_));
    }
    [[nodiscard]] lv_subject_t *get() noexcept
    {
        return &subject_;
    }

private:
    lv_subject_t subject_{};
};

class RevisionSubject {
public:
    RevisionSubject() : value_(0)
    {
    }

    void publish() noexcept
    {
        const auto current = value_.value();
        value_.set(current == std::numeric_limits<std::int32_t>::max() ? 0 : current + 1);
    }

    [[nodiscard]] lv_subject_t *get() noexcept
    {
        return value_.get();
    }

private:
    IntSubject value_;
};

}  // namespace lilygo::settings::components

#endif
