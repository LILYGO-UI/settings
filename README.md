# LILYGO UI Settings

`lilygo-ui-settings` is the standalone Settings application for LILYGO Linux
products. It uses the application ID `cc.lilygo.ui.Settings` and follows the
Settings layout: Wi-Fi, Bluetooth, Ethernet, Cellular, and Battery share one group;
General is a separate group, with Keyboard vibration controls and the About
page available from Settings.

The project builds independently from the launcher. It consumes LILYGO UI
AppKit through its source SDK CMake package and does not use launcher sources.

## UI architecture

The application uses a C++17 Model/ViewModel/View split. Shared platform state
and rules live in `src/domain/`, each page keeps its ViewModel and View in
`src/pages/<page>/`, and `src/app.cpp` plus `src/app_router.cpp` own lifecycle
and navigation. Each page View creates and owns its responsive LVGL object tree
under the parent supplied by its caller, then binds that tree to observable
ViewModel state. Reusable Settings controls and styles live in
`src/components/settings_widgets.*` and the other shared component modules.

Display text uses AppKit's runtime FreeType font chain through
`lilygo_ui_font_get()`. AppKit supplies Inter, Source Han Sans CN, and Font
Awesome fallback glyphs; the application does not bundle generated fonts or
refer to LVGL's built-in Montserrat fonts.

The UI creates one `settings_surface` below the AppKit-provided
`cm0_app_context_t::root`. All pages remain children of that surface and route
by changing visibility, so the application never replaces the runtime screen.
When the surface is wider than it is tall, it switches to a Grid layout with a
scrollable category sidebar on the left and the active page on the right;
portrait and square surfaces keep the single-page navigation flow. Resizing
the surface changes that arrangement in place and keeps the active page state.
AppKit alone owns system chrome and shared fonts; the Settings layout does not
reserve or render a second status bar or home indicator.

## Requirements

- CMake 3.21 or newer
- A C and C++ compiler
- `pkg-config`
- SDL2 for host simulation
- LPM 0.1.0 or newer, available as `lpm`
- `clang-format` for the optional pre-commit formatting hook
- Git submodules initialized
- An AArch64 cross compiler for device builds

AppKit is pinned as the `third_party/cm0-appkit` Git submodule. Initialize it
before configuring the application:

```sh
git submodule update --init --recursive
```

The presets select this source SDK with `LilyGoUI_DIR`. Its package config
builds AppKit and LVGL as static libraries and links them into each application.
Updating AppKit code requires updating the submodule revision and rebuilding
the application. A local SDK checkout can be selected explicitly:

```sh
cmake --preset host-simulator -DLilyGoUI_DIR=/path/to/appkit
```

Host builds use the SDK's font assets. Devices install `lilygo-ui-appkit-dev`,
version 0.1.0 or newer, which contains the source SDK and the shared Inter,
Source Han Sans CN, Font Awesome, and font licenses. Applications do not bundle
duplicate fonts. AppKit and LVGL remain statically linked into each application;
the package supplies no AppKit or LVGL shared libraries. The
`min_appkit_version` field in `lpm.toml` sets this package's minimum version.
The package also supports build environments using an installed source SDK
instead of the submodule.

Cross builds require the BSP sysroot and its system development libraries;
they do not require an AppKit SDK package in the sysroot.
When returning from a binary SDK build, use a fresh build directory so no old
imported targets or SDK search paths remain cached.

## Project metadata

[`lpm.toml`](lpm.toml) is the single developer-maintained source for the
application identity, version, descriptions, permissions, compatibility,
assets, build presets, and deployment defaults. CMake validates it through LPM
and derives the generated identity header, Launcher manifest, desktop entry,
AppStream metadata, and Debian package fields from it.

The source repository and homepage are
`https://github.com/LILYGO-UI/settings`; release metadata identifies the
application license as `LicenseRef-proprietary`. Update those values only in
`lpm.toml` so generated publishing and package metadata remain consistent.

Validate the metadata or generate temporary public publishing metadata with:

```sh
lpm metadata --format cmake
lpm metadata --format publish-json --output .lpm/publish.json
```

`publish.json` is generated output and is ignored by Git. To enable automatic
C/C++ formatting for commits, configure the checked-in hook once:

```sh
git config core.hooksPath .githooks
```

## Host simulator and tests

On macOS, install host dependencies with `brew install cmake pkg-config sdl2`.
Then configure, build, test, and launch the SDL simulator:

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
./build/host-simulator/lilygo-ui-settings
# Or, after an initial build:
lpm start --no-build --foreground
```

The test suite covers backend parsing and mutation plus every settings page at
the product's 568x1232 portrait resolution. It also renders 320x568 compact,
1232x568 landscape, and 1024x768 desktop viewports, and verifies that a live
orientation change preserves the active page and text input. Render tests
write PPM snapshots under `build/host-simulator/`.

## CM0 build and Debian package

Initialize the AppKit submodule, then use the cross preset.
On its first configuration, CMake downloads the pinned
[`0.1.0` CM0 BSP](https://github.com/LILYGO-UI/CM0BspBuilder/releases/download/0.1.0/cm0_sdk.tar.gz),
verifies its SHA-256 checksum, and extracts it under
`.cache/cm0-bsp/0.1.0`. Later builds reuse that cache.

```sh
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -B dist
```

The package is generated as
`dist/lilygo-ui-settings_<version>_arm64.deb`. It installs the executable to
`/usr/bin/lilygo-ui-settings`, the launcher manifest to
`/usr/share/launcher/apps/cc.lilygo.ui.Settings.json`, and its launcher icon to
`/usr/share/icons/hicolor/128x128/apps/cc.lilygo.ui.Settings.png`. It also
installs the BlueZ D-Bus helper under `/usr/libexec`,
`cc.lilygo.ui.Settings.desktop`, AppStream metadata, and AppKit font license
notices.

The package intentionally provides, conflicts with, and replaces the published
`lilygo-cm0-settings` package so Debian upgrades remove the old identity. These
migration fields are not a naming convention for new applications.

Device builds use AppKit's direct DRM/KMS display backend and evdev input.
They do not require a desktop session, Wayland, or a compositor. The generated
Debian package directly depends on DRM/FreeType system libraries and the
`lilygo-ui-appkit-dev` SDK and font package. The direct DRM backend needs no
Wayland or xkbcommon runtime libraries.

When the CM0 physical keyboard is connected, AppKit assigns focus to the
Settings buttons, switches, sliders, and text fields. `Tab` and `Shift+Tab`
move through the controls, `Enter` activates the focused control, and `Esc`
cancels text entry or leaves the application. Character keys are sent to the
focused text field; the software keyboard is hidden while the physical
keyboard is available, while applications can continue to show it when no
physical keyboard is present.

The published BSP sysroot contains the `libdrm-dev` headers and pkg-config
metadata required by the device build.

## GPU renderer builds

Device rendering is selected at application build time by the source AppKit
SDK. Use an updated SDK supporting `LILYGO_UI_RENDERER` and a target-matched
CM0 sysroot with `libgbm-dev`, `libegl-dev`, and `libgles-dev`. The original
BSP 0.1.0 lacks those GPU development dependencies. The original pinned
AppKit submodule does not support GPU renderer selection; use a newer source
SDK explicitly for this build.

Select the SDK source checkout or the directory containing the installed
source SDK's `LilyGoUIConfig.cmake`:

```sh
cmake --preset cm0-cross -B build/cm0-opengles \
  -DLilyGoUI_DIR=/path/to/appkit \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/cm0-sysroot/usr/share/cm0-bsp/toolchain.cmake \
  -DLILYGO_UI_RENDERER=opengles
cmake --build build/cm0-opengles --parallel
cpack --config build/cm0-opengles/CPackConfig.cmake -B dist/opengles
```

The executable contains AppKit and LVGL code, and its Debian package declares
the selected renderer's system graphics dependencies. Keep separate build
directories for software, OpenGL ES, and experimental NanoVG. Use software
rendering for host simulator tests. Compare rendering correctness, frame time,
memory, and repeated application exit/return on the target before changing a
release renderer.

## Platform integration

- Wi-Fi separates the current connection, known networks, and other nearby
  networks. NetworkManager stores joined networks and credentials on the device;
  Settings reads existing profiles without requesting their secrets. Known
  networks remain listed when out of range, and reconnect by profile UUID without
  asking for the password again. Tap the current network or its information
  button for read-only TCP/IP, DNS, and Hardware tabs. These show the actual
  IPv4/IPv6 configuration, addresses, subnet mask, routers, DNS servers, search
  domains, interface, MAC address, and MTU. Missing values are shown as `--`.
  Forget This Network requires confirmation and deletes only the selected
  profile. Joining and forgetting networks use `sudo -n` for existing administrator
  users, with a narrow policy installed as `/etc/sudoers.d/lilygo-ui-settings-wifi`
  (mode `0440`). Deploy the complete package to install that policy; missing
  authorization is reported as a failure. Passwords are supplied through stdin,
  never through process arguments. Radio control retains the platform's existing
  NetworkManager/rfkill authorization.
- Wi-Fi and Ethernet use `/sys/class/net`, `getifaddrs()`, and NetworkManager's
  `nmcli`. Ethernet exposes an inline IPv4 mode selector for DHCP or manual
  configuration. In DHCP mode, IP address, subnet mask, and Gateway are locked;
  manual mode enables those fields. DNS remains editable in either mode. The
  editor validates values locally, updates the selected NetworkManager
  connection persistently, and then activates it. Writes use
  `sudo -n`; the package installs a narrow `/etc/sudoers.d` policy for the two
  fixed `nmcli` operations. If the policy is absent or authorization fails,
  the page reports the save failure without prompting for or storing a password.
- Bluetooth uses `/sys/class/rfkill` and BlueZ's `bluetoothctl` for radio and
  discovery control. Device enumeration and friendly names use the packaged
  `org.bluez.Device1` D-Bus helper; devices without a published `Name` or
  `Alias` remain identifiable by their MAC address.
- Battery identifies the BQ27220 fuel gauge and BQ25890 charger separately through
  `/sys/class/power_supply`. It shows charge percentage, battery health, remaining,
  full and design capacities, cycle count, voltage, current and temperature.
  Maximum Capacity is calculated from full / design capacity; it is not a separate
  hardware SOH register measurement.
  Battery Calibration edits the design and visible full-charge capacities in mAh,
  validates both values, asks for confirmation, and verifies readback. This needs
  the accompanying [BQ27220 driver patch](data/kernel/README.md): the existing board
  driver exposes these values as read-only. Old drivers display an upgrade notice.
  The paired `calibration_capacity` ABI applies both values in one configuration
  transaction; it does not write OTP or change the battery chemistry profile.
  Capacity settings are in gauge RAM and can be lost when the gauge loses power.
  The optional learning-cycle guide observes charge / use / recharge and does not
  claim that the gauge completed a qualified CEDV learning cycle.
  BQ25890 controls adjust input current (100–3250 mA, 50 mA steps) and charge
  current (64 mA steps, capped by the device-tree battery limit). Choose an input
  limit appropriate for the power source and cable. Changes are applied explicitly
  and checked against the actual hardware value; thermal derating can produce a
  different readback. The app does not write `online` (which controls input Hi-Z),
  charge voltage, or raw I2C registers. Limits can be reset by the charger/driver
  after reconnecting power or restarting.
  The packaged `/usr/libexec/lilygo-ui-settings-power` helper accepts only the
  validated capacity pair and these two current controls, using a fixed sysfs root.
  `/etc/sudoers.d/lilygo-ui-settings-power` permits existing `sudo` group users to
  invoke those operations through `sudo -n`; UI commands run asynchronously.
  Install the complete Debian package to include the helper and policy. A platform
  using `no_new_privs` must provide equivalent authorization. Custom test sysfs
  roots are never forwarded to the privileged helper.
- General → Sharing provides a Remote Login switch for SSH. Turning it on runs
  `systemctl enable --now ssh.service`; turning it off disables and stops
  `ssh.service` and any installed `ssh.socket` listener. The setting persists
  across reboots. The switch follows observed service/socket state, refreshes
  periodically, and stays disabled while applying or verifying a user change.
  Background refreshes keep both Sharing switches focusable and responsive to
  physical-button input; a user action takes precedence over a pending refresh.
  Results are verified by reading back both runtime and startup state. A nonzero
  command exit is not reported as a failure when the complete requested state
  is confirmed. Original command errors are written to the application log.
  The target needs systemd, OpenSSH server, and sudo. The Debian package installs
  `/etc/sudoers.d/lilygo-ui-settings-ssh` (mode `0440`), allowing existing `sudo`
  group members to run only the three exact SSH enable/disable commands used by
  this page without a password. Writes use `sudo -n`; reads remain unprivileged.
  The rule grants no access to other services or arbitrary commands. Settings
  does not install SSH, unmask units, change login authentication, or add users
  to the administrator group. Deploy the complete package so this rule is included.
  Missing services and access failures appear on the Sharing page.
  `LILYGO_UI_SYSTEMCTL` and `LILYGO_UI_SUDO` override the executables for platform
  integration and isolated tests; arguments are passed directly without a shell.
  Alternate controller paths need their own platform authorization; the bundled
  rule authorizes only `/usr/bin/systemctl` with the fixed SSH arguments.
  A supervisor that starts applications with Linux `no_new_privs` prevents
  `sudo` from elevating; such a platform must provide an equivalent privileged
  broker for this control.
- Sharing also provides a Serial Console switch for boot messages and serial
  login. It uses `raspi-config nonint get_serial_cons` and `do_serial_cons 0/1`
  (0 enables, 1 disables) to change the console independently of UART hardware,
  following the [Raspberry Pi serial configuration documentation](https://www.raspberrypi.com/documentation/computers/configuration.html#serial-port).
  The target needs a version of `raspi-config` that supports these commands.
  The package installs `/etc/sudoers.d/lilygo-ui-settings-serial` (mode `0440`),
  allowing existing `sudo` group members to run only `do_serial_cons 0`,
  `do_serial_cons 1`, and `systemctl --no-ask-password reboot` via `sudo -n`.
  Reading the saved state remains unprivileged; no password is requested or stored.
  Install the complete package to include the authorization rule. After a successful save and
  readback, choose **Restart Now** or **Restart Later**. Only Restart Now sends
  `systemctl --no-ask-password reboot`; failures allow retrying or postponing.
  Restart Later retains the saved setting. Comparing it with `/proc/cmdline`
  keeps the pending-restart status visible after reopening Settings.
  `LILYGO_UI_RASPI_CONFIG` and `LILYGO_UI_RUNNING_CMDLINE` override the tool and
  running command-line path for platform integration and isolated tests;
  `LILYGO_UI_SUDO` selects the authorization executable. The packaged policy
  only matches the fixed `/usr/bin/raspi-config` and `/usr/bin/systemctl` commands.
- Cellular accesses the Nordic nRF9151 Serial LTE Modem (SLM) directly through
  `/dev/ttyACM0` at 115200 baud, 8 data bits, no parity, and 1 stop bit. The switch
  uses the modem's supported `AT+CFUN=21` / `AT+CFUN=20` commands to enable or
  disable LTE while preserving GNSS, then reads back the actual functional and
  system modes. It does not configure APNs or activate PDP contexts.
  `AT+CPIN?`, `AT%XICCID`, `AT+CIMI`, `AT+COPS?`, `AT+CEREG?`, and `AT+CESQ`
  supply SIM status, ICCID, IMSI, operator, LTE-M / NB-IoT technology, registration,
  and RSRP signal strength. Missing devices, serial access failures, busy ports,
  missing SIMs, and PIN / PUK locks are shown explicitly.
  Data Usage optionally reads the modem's `AT%XCONNSTAT?` collection counters.
  The transmit and receive fields are reported in kB and displayed using decimal
  units. Settings does not start, stop, or reset collection: values can remain
  unchanged when collection is stopped and represent the modem's collection
  period, not monthly billing or remaining plan allowance. Unsupported or
  unreadable statistics are shown as unavailable.
  The operating system must grant the app's user read/write access to the serial
  device, for example through its serial-device group or an appropriate udev
  rule. This page does not use sudo, NetworkManager, or ModemManager. It shares
  `/tmp/lilygo_nrf9151_uart.lock` with `lilygo-ui-cellular` for the default port;
  close the other app's modem session before accessing it from Settings.
  `LILYGO_UI_CELLULAR_DEVICE` overrides the serial path for platform integration
  and PTY tests. Protocol details follow the
  [nRF91x1 Cellular AT Commands reference](https://docs.nordicsemi.com/r/bundle/ref_at_commands_nrf91x1/).
- About reads OS, device-tree, memory, kernel, and filesystem information.

The UI remains usable when hardware or an optional service is absent and shows
an explicit unavailable state. Platform images and tests can override paths
with `CM0_NETWORK_DIR`, `CM0_RFKILL_DIR`, `CM0_POWER_SUPPLY_DIR`, `CM0_NMCLI`,
`CM0_BLUETOOTHCTL`, `CM0_OS_RELEASE_FILE`, `CM0_DEVICE_MODEL_FILE`,
`CM0_DEVICE_SERIAL_FILE`, `CM0_MEMINFO_FILE`, `CM0_STORAGE_PATH`, and
`LILYGO_UI_BLUETOOTH_DBUS_HELPER`.
