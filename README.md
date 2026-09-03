# LILYGO UI Settings

`lilygo-ui-settings` is the standalone Settings application for LILYGO Linux
products. It uses the application ID `cc.lilygo.ui.Settings` and follows the
Settings layout: Wi-Fi, Bluetooth, Ethernet, and Battery share one group;
General is a separate group and contains the About page.

The project builds independently from the launcher. It consumes LILYGO UI
AppKit as a CMake package and does not vendor launcher or AppKit sources.

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

AppKit is pinned as the `third_party/cm0-appkit` Git submodule. Clone with
submodules, or initialize them in an existing checkout:

```sh
git clone --recurse-submodules <settings-repository-url>
git submodule update --init --recursive
```

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
git submodule update --init --recursive
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

Initialize the AppKit submodule, then use the cross preset. On its first
configuration, CMake downloads the pinned
[`0.1.0` CM0 BSP](https://github.com/LILYGO-UI/CM0BspBuilder/releases/download/0.1.0/cm0_sdk.tar.gz),
verifies its SHA-256 checksum, and extracts it under
`.cache/cm0-bsp/0.1.0`. Later builds reuse that cache.

```sh
git submodule update --init --recursive
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -B dist
```

The package is generated as
`dist/lilygo-ui-settings_<version>_arm64.deb`. It installs the executable to
`/usr/bin/lilygo-ui-settings`, the launcher manifest to
`/usr/share/launcher/apps/cc.lilygo.ui.Settings.json`, and its launcher icon to
`/usr/share/icons/hicolor/128x128/apps/cc.lilygo.ui.Settings.png`. It also
installs `cc.lilygo.ui.Settings.desktop`, AppStream metadata, and AppKit font
license notices.

The package intentionally provides, conflicts with, and replaces the published
`lilygo-cm0-settings` package so Debian upgrades remove the old identity. These
migration fields are not a naming convention for new applications.

Device builds use AppKit's direct DRM/KMS display backend and evdev input.
They do not require a desktop session, Wayland, or a compositor. The generated
Debian package depends on `libdrm2` but has no Wayland or xkbcommon runtime
dependencies.

The published BSP sysroot contains the `libdrm-dev` headers and pkg-config
metadata required by the device build.

## Platform integration

- Wi-Fi and Ethernet use `/sys/class/net`, `getifaddrs()`, and NetworkManager's
  `nmcli`.
- Bluetooth uses `/sys/class/rfkill` and BlueZ's `bluetoothctl`.
- Battery uses `/sys/class/power_supply`.
- About reads OS, device-tree, memory, kernel, and filesystem information.

The UI remains usable when hardware or an optional service is absent and shows
an explicit unavailable state. Platform images and tests can override paths
with `CM0_NETWORK_DIR`, `CM0_RFKILL_DIR`, `CM0_POWER_SUPPLY_DIR`, `CM0_NMCLI`,
`CM0_BLUETOOTHCTL`, `CM0_OS_RELEASE_FILE`, `CM0_DEVICE_MODEL_FILE`,
`CM0_DEVICE_SERIAL_FILE`, `CM0_MEMINFO_FILE`, and `CM0_STORAGE_PATH`.
