cmake_minimum_required(VERSION 3.21)

include(cmake/LpmMetadata.cmake)
project(${LILYGO_UI_CMAKE_PROJECT_NAME} LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)
find_package(LilyGoUI CONFIG REQUIRED)

configure_file(
    cmake/app_identity.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/app_identity.h
    @ONLY)

add_library(lilygo_ui_settings_core STATIC
    src/domain/settings_model.cpp
    src/domain/settings_platform.cpp)
target_include_directories(lilygo_ui_settings_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(lilygo_ui_settings_core PRIVATE
    -Wall -Wextra -Wpedantic)
target_compile_features(lilygo_ui_settings_core PUBLIC cxx_std_17)

add_library(lilygo_ui_settings_app STATIC
    src/app.cpp
    src/app_router.cpp
    src/assets/settings_icons.cpp
    src/components/settings_widgets.cpp
    src/components/ui_helpers.cpp
    src/domain/settings_command_runner.cpp
    src/pages/about/about_view.cpp
    src/pages/about/about_view_model.cpp
    src/pages/battery/battery_view.cpp
    src/pages/battery/battery_view_model.cpp
    src/pages/bluetooth/bluetooth_view.cpp
    src/pages/bluetooth/bluetooth_view_model.cpp
    src/pages/ethernet/ethernet_view.cpp
    src/pages/ethernet/ethernet_view_model.cpp
    src/pages/general/general_view.cpp
    src/pages/general/general_view_model.cpp
    src/pages/home/home_view.cpp
    src/pages/home/home_view_model.cpp
    src/pages/settings/settings_view.cpp
    src/pages/settings/settings_view_model.cpp
    src/pages/wifi/wifi_view.cpp
    src/pages/wifi/wifi_view_model.cpp)
target_include_directories(lilygo_ui_settings_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(lilygo_ui_settings_app PRIVATE
    -Wall -Wextra -Wpedantic)
target_compile_features(lilygo_ui_settings_app PUBLIC cxx_std_17)
target_link_libraries(lilygo_ui_settings_app PUBLIC
    lilygo_ui_settings_core
    LilyGoUI::Runtime)

add_executable(lilygo_ui_settings src/main.cpp)
target_compile_options(lilygo_ui_settings PRIVATE
    -Wall -Wextra -Wpedantic)
target_compile_features(lilygo_ui_settings PRIVATE cxx_std_17)
target_link_libraries(lilygo_ui_settings PRIVATE lilygo_ui_settings_app)
set_target_properties(lilygo_ui_settings PROPERTIES
    OUTPUT_NAME "${LILYGO_UI_EXECUTABLE_NAME}")

configure_file(data/launcher.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json @ONLY)
configure_file(data/application.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop @ONLY)
configure_file(data/application.metainfo.xml.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml @ONLY)

install(TARGETS lilygo_ui_settings
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT app)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/launcher/apps
    COMPONENT app)
lilygo_ui_install_launcher_icon(
    "${LILYGO_UI_APP_ID}"
    "${CMAKE_CURRENT_SOURCE_DIR}/${LILYGO_UI_APP_ICON}")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    COMPONENT app)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo
    COMPONENT app)
lilygo_ui_install_font_licenses(
    "${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_PACKAGE_NAME}"
    app)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(settings-core-tests tests/test_core.cpp)
    target_compile_options(settings-core-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(settings-core-tests PRIVATE
        lilygo_ui_settings_core)
    add_test(NAME settings-core COMMAND settings-core-tests)

    add_executable(settings-view-model-tests tests/test_view_model.cpp)
    target_compile_options(settings-view-model-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(settings-view-model-tests PRIVATE
        lilygo_ui_settings_app)
    add_test(NAME settings-view-model COMMAND settings-view-model-tests)

    add_executable(settings-render-ui tests/render_settings.cpp)
    target_compile_options(settings-render-ui PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(settings-render-ui PRIVATE
        lilygo_ui_settings_app)
    foreach(page IN ITEMS
            home wifi wifi-password wifi-connect wifi-secured-connect
            bluetooth bluetooth-connect ethernet battery general about)
        add_test(NAME settings-render-${page} COMMAND settings-render-ui
            ${page} ${CMAKE_CURRENT_BINARY_DIR}/settings-${page}.ppm)
    endforeach()
    add_test(NAME settings-render-compact-home COMMAND settings-render-ui
        home ${CMAKE_CURRENT_BINARY_DIR}/settings-compact-home.ppm 320 568)
    add_test(NAME settings-render-compact-wifi-password COMMAND settings-render-ui
        wifi-password
        ${CMAKE_CURRENT_BINARY_DIR}/settings-compact-wifi-password.ppm 320 568)
    foreach(page IN ITEMS home wifi bluetooth ethernet battery general about)
        add_test(NAME settings-render-landscape-${page} COMMAND settings-render-ui
            ${page} ${CMAKE_CURRENT_BINARY_DIR}/settings-landscape-${page}.ppm
            1232 568)
    endforeach()
    add_test(NAME settings-render-desktop-about COMMAND settings-render-ui
        about ${CMAKE_CURRENT_BINARY_DIR}/settings-desktop-about.ppm 1024 768)
    add_test(NAME settings-responsive-resize-state COMMAND settings-render-ui
        resize-state ${CMAKE_CURRENT_BINARY_DIR}/settings-resize-state.ppm)
    add_test(NAME settings-close-active-command COMMAND settings-render-ui
        close-active-command
        ${CMAKE_CURRENT_BINARY_DIR}/settings-close-active-command.ppm)
    set_tests_properties(settings-close-active-command PROPERTIES TIMEOUT 5)
    add_test(NAME settings-open-close-reopen-resize COMMAND settings-render-ui
        reopen-resize
        ${CMAKE_CURRENT_BINARY_DIR}/settings-reopen-resize.ppm)
endif()

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME "${LILYGO_UI_PACKAGE_NAME}")
set(CPACK_PACKAGE_VERSION "${LILYGO_UI_VERSION}")
if(LILYGO_UI_VERSION MATCHES
   "^([0-9]+:)?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(CPACK_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_2}")
    set(CPACK_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_3}")
    set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_MATCH_4}")
else()
    set(CPACK_PACKAGE_VERSION_MAJOR 0)
    set(CPACK_PACKAGE_VERSION_MINOR 0)
    set(CPACK_PACKAGE_VERSION_PATCH 0)
endif()
set(CPACK_PACKAGE_CONTACT "${LILYGO_UI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_VENDOR "${LILYGO_UI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${LILYGO_UI_APP_SUMMARY}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${LILYGO_UI_APP_HOMEPAGE}")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_SET_DESTDIR ON)
set(CPACK_COMPONENTS_ALL app)
set(CPACK_DEBIAN_APP_PACKAGE_NAME "${LILYGO_UI_PACKAGE_NAME}")
set(CPACK_DEBIAN_APP_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_APP_PACKAGE_DEPENDS
    "libc6, libstdc++6, libdrm2, libfreetype6, lilygo-ui-appkit-dev (>= ${LILYGO_UI_MIN_APPKIT_VERSION})")
set(CPACK_DEBIAN_APP_PACKAGE_SECTION "utils")

# lilygo-ui-settings supersedes the published lilygo-cm0-settings package.
# Keep these fields until supported upgrades no longer need the old identity.
set(CPACK_DEBIAN_APP_PACKAGE_PROVIDES
    "lilygo-cm0-settings (= ${LILYGO_UI_VERSION})")
set(CPACK_DEBIAN_APP_PACKAGE_CONFLICTS "lilygo-cm0-settings")
set(CPACK_DEBIAN_APP_PACKAGE_REPLACES "lilygo-cm0-settings")

set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE
        "${LILYGO_UI_PACKAGE_ARCHITECTURE}")
endif()
include(CPack)
