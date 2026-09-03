#include "app.hpp"

#include "app_identity.h"
#include "app_router.hpp"
#include "pages/settings/settings_view.hpp"
#include "pages/settings/settings_view_model.hpp"

#include <cstdio>
#include <memory>

namespace {

struct AppState {
    std::unique_ptr<lilygo::settings::SettingsViewModel> view_model;
    std::unique_ptr<lilygo::settings::AppRouter> router;
    std::unique_ptr<lilygo::settings::SettingsView> view;
};

AppState app_state;

void app_close();

void app_open(const cm0_app_context_t *context)
{
    app_close();
    if (!context || !context->root) {
        std::fprintf(stderr, "[settings] missing AppKit root object\n");
        return;
    }
    app_state.view_model = std::make_unique<lilygo::settings::SettingsViewModel>();
    app_state.router     = std::make_unique<lilygo::settings::AppRouter>();
    app_state.view       = std::make_unique<lilygo::settings::SettingsView>(*app_state.view_model, *app_state.router);
    if (!app_state.view->create(context->root)) app_close();
}

void app_close()
{
    if (app_state.view) app_state.view->destroy();
    app_state.view.reset();
    app_state.router.reset();
    app_state.view_model.reset();
}

const cm0_app_descriptor_t descriptor{
    CM0_APP_API_VERSION, sizeof(cm0_app_descriptor_t), LILYGO_UI_SETTINGS_APP_ID, app_open, app_close,
};

}  // namespace

extern "C" const cm0_app_descriptor_t *cm0_app_get_descriptor()
{
    return &descriptor;
}
