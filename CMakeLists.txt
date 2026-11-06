cmake_minimum_required(VERSION 3.21)

include(cmake/LpmMetadata.cmake)
project(${LILYGO_UI_CMAKE_PROJECT_NAME} LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)
# Match the 50 Hz panel in LVGL, the statically linked SDK, and this application.
add_compile_definitions(LV_DEF_REFR_PERIOD=20)
find_package(LilyGoUI CONFIG REQUIRED)
find_package(Threads REQUIRED)

set(LILYGO_UI_BATTERY_HELPER_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/${LILYGO_UI_PACKAGE_NAME}-power")

configure_file(
    cmake/app_identity.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/app_identity.h
    @ONLY)

add_library(lilygo_ui_settings_core STATIC
    src/pages/battery/battery_model.cpp
    src/pages/wifi/wifi_model.cpp
    src/pages/cellular/cellular_model.cpp
    src/pages/cellular/cellular_serial.cpp
    src/pages/keyboard/keyboard_model.cpp
    src/pages/sharing/sharing_model.cpp
    src/pages/sharing/serial_console_model.cpp
    src/domain/settings_model.cpp
    src/domain/settings_platform.cpp)
target_include_directories(lilygo_ui_settings_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(lilygo_ui_settings_core PRIVATE
    -Wall -Wextra -Wpedantic)
target_compile_features(lilygo_ui_settings_core PUBLIC cxx_std_17)
target_link_libraries(lilygo_ui_settings_core PUBLIC Threads::Threads)

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
    src/pages/cellular/cellular_view.cpp
    src/pages/cellular/cellular_view_model.cpp
    src/pages/general/general_view.cpp
    src/pages/general/general_view_model.cpp
    src/pages/home/home_view.cpp
    src/pages/home/home_view_model.cpp
    src/pages/keyboard/keyboard_view.cpp
    src/pages/keyboard/keyboard_view_model.cpp
    src/pages/sharing/sharing_view.cpp
    src/pages/sharing/sharing_view_model.cpp
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

add_executable(lilygo_ui_settings_power src/pages/battery/battery_control_main.cpp)
target_link_libraries(lilygo_ui_settings_power PRIVATE lilygo_ui_settings_core)
target_compile_options(lilygo_ui_settings_power PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(lilygo_ui_settings_power PROPERTIES OUTPUT_NAME "${LILYGO_UI_PACKAGE_NAME}-power")

# BlueZ exposes device names through org.bluez.Device1 on the system D-Bus.
# The helper loads libsystemd's public sd-bus ABI at runtime so the source
# build does not need to vendor a target-specific development sysroot.
add_executable(lilygo_ui_settings_bluetooth src/tools/bluetooth_dbus_main.cpp)
target_compile_options(lilygo_ui_settings_bluetooth PRIVATE -Wall -Wextra -Wpedantic)
target_compile_features(lilygo_ui_settings_bluetooth PRIVATE cxx_std_17)
target_link_libraries(lilygo_ui_settings_bluetooth PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(lilygo_ui_settings_bluetooth PROPERTIES
    OUTPUT_NAME "${LILYGO_UI_PACKAGE_NAME}-bluetooth")

configure_file(data/power.sudoers.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-power.sudoers @ONLY)
configure_file(data/launcher.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json @ONLY)
configure_file(data/application.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop @ONLY)
configure_file(data/application.metainfo.xml.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml @ONLY)
configure_file(data/ssh.sudoers.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-ssh @ONLY)
configure_file(data/serial.sudoers.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-serial @ONLY)
configure_file(data/ethernet.sudoers.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-ethernet @ONLY)
configure_file(data/wifi.sudoers.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-wifi @ONLY)

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
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # sudo only reads policy files from the host's /etc/sudoers.d.  Keep this
    # path absolute: GNUInstallDirs leaves CMAKE_INSTALL_SYSCONFDIR relative
    # ("etc") for the /usr device prefix, which would otherwise package the
    # policy under /usr/etc/sudoers.d where sudo ignores it.
    install(TARGETS lilygo_ui_settings_power RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR} COMPONENT app)
    install(TARGETS lilygo_ui_settings_bluetooth RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR} COMPONENT app)
    set(LILYGO_UI_SUDOERS_DIR "/etc/sudoers.d")
    configure_file(data/conffiles.in ${CMAKE_CURRENT_BINARY_DIR}/conffiles @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-power.sudoers
        DESTINATION ${LILYGO_UI_SUDOERS_DIR}
        RENAME ${LILYGO_UI_PACKAGE_NAME}-power
        PERMISSIONS OWNER_READ GROUP_READ
        COMPONENT app)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-ssh
        ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-serial
        ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-ethernet
        ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-wifi
        DESTINATION ${LILYGO_UI_SUDOERS_DIR}
        PERMISSIONS OWNER_READ GROUP_READ
        COMPONENT app)
endif()
lilygo_ui_install_font_licenses(
    "${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_PACKAGE_NAME}"
    app)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    find_program(VISUDO_EXECUTABLE NAMES visudo HINTS /usr/sbin /sbin)
    if(VISUDO_EXECUTABLE)
        add_test(NAME settings-power-policy-syntax COMMAND ${CMAKE_COMMAND} -E env ${VISUDO_EXECUTABLE}
            -cf ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-power.sudoers)
        add_test(NAME settings-ssh-policy-syntax COMMAND ${CMAKE_COMMAND} -E env ${VISUDO_EXECUTABLE}
            -cf ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-ssh)
        add_test(NAME settings-serial-policy-syntax COMMAND ${CMAKE_COMMAND} -E env ${VISUDO_EXECUTABLE}
            -cf ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-serial)
        add_test(NAME settings-ethernet-policy-syntax COMMAND ${CMAKE_COMMAND} -E env ${VISUDO_EXECUTABLE}
            -cf ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-ethernet)
        add_test(NAME settings-wifi-policy-syntax COMMAND ${CMAKE_COMMAND} -E env ${VISUDO_EXECUTABLE}
            -cf ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_PACKAGE_NAME}-wifi)
    endif()
    foreach(argument IN ITEMS "../capacity" "input-current")
        string(MAKE_C_IDENTIFIER "${argument}" argument_id)
        add_test(NAME settings-power-reject-${argument_id} COMMAND lilygo_ui_settings_power "${argument}" "1;id" "1:1")
        set_tests_properties(settings-power-reject-${argument_id} PROPERTIES WILL_FAIL TRUE)
    endforeach()
    add_test(NAME settings-battery-driver-protocol COMMAND ${CMAKE_COMMAND} -E env
        "CXX=${CMAKE_CXX_COMPILER}" sh ${CMAKE_CURRENT_SOURCE_DIR}/data/kernel/tests/run.sh)
    add_executable(settings-battery-tests tests/test_battery.cpp)
    target_compile_options(settings-battery-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(settings-battery-tests PRIVATE lilygo_ui_settings_core)
    add_test(NAME settings-battery COMMAND settings-battery-tests)

    add_executable(settings-battery-view-model-tests tests/test_battery_view_model.cpp)
    target_compile_options(settings-battery-view-model-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(settings-battery-view-model-tests PRIVATE lilygo_ui_settings_app)
    add_test(NAME settings-battery-view-model COMMAND settings-battery-view-model-tests)

    add_executable(settings-cellular-tests tests/test_cellular.cpp)
    target_compile_options(settings-cellular-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(settings-cellular-tests PRIVATE lilygo_ui_settings_core)
    add_test(NAME settings-cellular COMMAND settings-cellular-tests)

    add_executable(settings-cellular-serial-tests tests/test_cellular_serial.cpp)
    target_compile_options(settings-cellular-serial-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(settings-cellular-serial-tests PRIVATE lilygo_ui_settings_core)
    add_test(NAME settings-cellular-serial COMMAND settings-cellular-serial-tests)

    add_executable(settings-cellular-view-model-tests tests/test_cellular_view_model.cpp)
    target_compile_options(settings-cellular-view-model-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(settings-cellular-view-model-tests PRIVATE lilygo_ui_settings_app)
    add_test(NAME settings-cellular-view-model COMMAND settings-cellular-view-model-tests)

    add_executable(settings-core-tests tests/test_core.cpp)
    target_compile_options(settings-core-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(settings-core-tests PRIVATE
        lilygo_ui_settings_core)
    add_test(NAME settings-core COMMAND settings-core-tests)

    add_executable(settings-command-runner-tests tests/test_command_runner.cpp)
    target_compile_options(settings-command-runner-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(settings-command-runner-tests PRIVATE lilygo_ui_settings_app)
    add_test(NAME settings-command-runner COMMAND settings-command-runner-tests)

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
            home wifi wifi-password wifi-connect wifi-secured-connect wifi-keyboard-connect
            wifi-details-tcpip wifi-details-dns wifi-details-hardware wifi-known-connect wifi-forget wifi-details-resize
            bluetooth bluetooth-connect ethernet cellular battery general about keyboard sharing)
        add_test(NAME settings-render-${page} COMMAND settings-render-ui
            ${page} ${CMAKE_CURRENT_BINARY_DIR}/settings-${page}.ppm)
    endforeach()
    foreach(scenario IN ITEMS battery-calibration battery-save battery-controls battery-unsupported battery-resize battery-keyboard)
        add_test(NAME settings-render-${scenario} COMMAND settings-render-ui
            ${scenario} ${CMAKE_CURRENT_BINARY_DIR}/settings-${scenario}.ppm)
    endforeach()
    foreach(size IN ITEMS compact landscape)
        if(size STREQUAL "compact")
            set(battery_width 320)
            set(battery_height 568)
        else()
            set(battery_width 1232)
            set(battery_height 568)
        endif()
        foreach(scenario IN ITEMS battery battery-calibration battery-controls battery-keyboard)
            if(NOT (size STREQUAL "landscape" AND scenario STREQUAL "battery"))
                add_test(NAME settings-render-${size}-${scenario} COMMAND settings-render-ui
                    ${scenario} ${CMAKE_CURRENT_BINARY_DIR}/settings-${size}-${scenario}.ppm ${battery_width} ${battery_height})
            endif()
        endforeach()
    endforeach()
    add_test(NAME settings-render-ethernet-config COMMAND settings-render-ui
        ethernet-config ${CMAKE_CURRENT_BINARY_DIR}/settings-ethernet-config.ppm)
    foreach(scenario IN ITEMS cellular-toggle cellular-unavailable cellular-no-sim cellular-denied cellular-resize)
        add_test(NAME settings-render-${scenario} COMMAND settings-render-ui
            ${scenario} ${CMAKE_CURRENT_BINARY_DIR}/settings-${scenario}.ppm)
    endforeach()
    add_test(NAME settings-render-compact-cellular COMMAND settings-render-ui
        cellular ${CMAKE_CURRENT_BINARY_DIR}/settings-compact-cellular.ppm 320 568)
    add_test(NAME settings-render-compact-home COMMAND settings-render-ui
        home ${CMAKE_CURRENT_BINARY_DIR}/settings-compact-home.ppm 320 568)
    add_test(NAME settings-render-compact-wifi-password COMMAND settings-render-ui
        wifi-password
        ${CMAKE_CURRENT_BINARY_DIR}/settings-compact-wifi-password.ppm 320 568)
    add_test(NAME settings-render-compact-wifi-details-tcpip COMMAND settings-render-ui
        wifi-details-tcpip ${CMAKE_CURRENT_BINARY_DIR}/settings-compact-wifi-details-tcpip.ppm 320 568)
    foreach(page IN ITEMS wifi-details-dns wifi-details-hardware wifi-known-connect wifi-forget)
        add_test(NAME settings-render-compact-${page} COMMAND settings-render-ui
            ${page} ${CMAKE_CURRENT_BINARY_DIR}/settings-compact-${page}.ppm 320 568)
    endforeach()
    add_test(NAME settings-render-compact-keyboard COMMAND settings-render-ui
        keyboard ${CMAKE_CURRENT_BINARY_DIR}/settings-compact-keyboard.ppm 320 568)
    foreach(page IN ITEMS home wifi wifi-details-tcpip wifi-details-dns wifi-details-hardware bluetooth ethernet cellular battery general about keyboard sharing)
        add_test(NAME settings-render-landscape-${page} COMMAND settings-render-ui
            ${page} ${CMAKE_CURRENT_BINARY_DIR}/settings-landscape-${page}.ppm
            1232 568)
    endforeach()
    add_test(NAME settings-render-compact-sharing COMMAND settings-render-ui
        sharing ${CMAKE_CURRENT_BINARY_DIR}/settings-compact-sharing.ppm 320 568)
    add_test(NAME settings-render-sharing-toggle COMMAND settings-render-ui
        sharing-toggle ${CMAKE_CURRENT_BINARY_DIR}/settings-sharing-toggle.ppm)
    add_test(NAME settings-render-sharing-denied COMMAND settings-render-ui
        sharing-denied ${CMAKE_CURRENT_BINARY_DIR}/settings-sharing-denied.ppm)
    add_test(NAME settings-render-compact-sharing-denied COMMAND settings-render-ui
        sharing-denied ${CMAKE_CURRENT_BINARY_DIR}/settings-compact-sharing-denied.ppm 320 568)
    add_test(NAME settings-render-landscape-sharing-denied COMMAND settings-render-ui
        sharing-denied ${CMAKE_CURRENT_BINARY_DIR}/settings-landscape-sharing-denied.ppm 1232 568)
    add_test(NAME settings-render-sharing-resize COMMAND settings-render-ui
        sharing-resize ${CMAKE_CURRENT_BINARY_DIR}/settings-sharing-resize.ppm)
    add_test(NAME settings-sharing-keyboard-focus COMMAND settings-render-ui
        sharing-focus ${CMAKE_CURRENT_BINARY_DIR}/settings-sharing-focus.ppm)
    add_test(NAME settings-sharing-landscape-keyboard-focus COMMAND settings-render-ui
        sharing-focus ${CMAKE_CURRENT_BINARY_DIR}/settings-sharing-landscape-focus.ppm 1232 568)
    foreach(scenario IN ITEMS serial-dialog serial-later serial-now serial-reboot-failure serial-resize)
        add_test(NAME settings-render-${scenario} COMMAND settings-render-ui
            ${scenario} ${CMAKE_CURRENT_BINARY_DIR}/settings-${scenario}.ppm)
    endforeach()
    add_test(NAME settings-render-compact-serial-dialog COMMAND settings-render-ui
        serial-dialog ${CMAKE_CURRENT_BINARY_DIR}/settings-compact-serial-dialog.ppm 320 568)
    add_test(NAME settings-render-landscape-serial-dialog COMMAND settings-render-ui
        serial-dialog ${CMAKE_CURRENT_BINARY_DIR}/settings-landscape-serial-dialog.ppm 1232 568)
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
    "bluez, libc6, libstdc++6, libdrm2, libfreetype6, libsystemd0, sudo (>= 1.9.10), lilygo-ui-appkit-dev (>= ${LILYGO_UI_MIN_APPKIT_VERSION})")
if(LILYGO_UI_GRAPHICS_DEBIAN_DEPENDS)
    string(APPEND CPACK_DEBIAN_APP_PACKAGE_DEPENDS
        ", ${LILYGO_UI_GRAPHICS_DEBIAN_DEPENDS}")
endif()
set(CPACK_DEBIAN_APP_PACKAGE_SECTION "utils")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_DEBIAN_APP_PACKAGE_CONTROL_EXTRA ${CMAKE_CURRENT_BINARY_DIR}/conffiles)
endif()

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
