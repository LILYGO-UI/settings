# LILYGO UI Settings 工程总览

本仓库是可独立配置、构建、测试、安装和打包的 LILYGO UI Settings
应用。本文中“必须”表示项目需要遵守的稳定约束，“按需”表示只在实际功能
需要时才创建，不使用空目录占位。

## 稳定约束

- 应用源码和测试使用 C++17 或更新标准；仅在 AppKit 集成边界保留 C ABI。
- UI 使用 LVGL 9.5，版本和补丁由 LilyGoUI SDK 统一提供，应用不自行引入
  LVGL 副本。
- 桌面预览使用 SDL2；设备默认使用 DRM/KMS。
- 标准测试视口为竖屏 `568x1232` 和横屏 `1232x568`。布局必须依据容器的
  实际几何尺寸响应，不得对外暴露应用展示模式。
- 横屏页面使用左侧可滚动分类栏和右侧当前页面的 Grid 分栏；竖屏和正方形页面
  保持单页导航，尺寸变化时保留当前页面状态。
- 首页和横屏分类栏按顺序分为三组：Wi-Fi / Bluetooth / Ethernet / Cellular / Battery、
  General、Keyboard。Ethernet 的标题和信息卡片随右侧内容区域宽度铺开。
- 顶层构建系统是 CMake 3.21 或更新版本，稳定预设名为 `host-simulator` 和
  `cm0-cross`。
- 应用只能通过 `find_package(LilyGoUI CONFIG REQUIRED)` 消费平台 SDK。不得引用
  Launcher 源码、其他应用仓库或 AppKit 私有文件。
- `/usr/share/launcher/apps` 下的 Launcher manifest 只是运行时发现协议，不是
  构建依赖。
- 公开接口不得使用 `cm0-app-*`、`lilygo-cm0-*`、`cc.lilygo.cm0.*` 或
  `org.cm0.*` 等旧命名。Debian 对已发布 `lilygo-cm0-settings` 的迁移字段是唯一
  明确例外。
- UI 必须使用 LVGL Flex 或 Grid 组织响应式布局。文本使用
  `<cm0/typography.h>` 中的 `lilygo_ui_font_get()` 统一获取 AppKit 字体。

## 开发环境

开发机必须提供以下工具：

- Git，用于获取仓库和初始化 Git submodule。
- CMake 3.21 或更新版本，并提供 `ctest` 和 `cpack`。
- 支持 C++17 的 C/C++ 编译器和 `make`。
- `pkg-config` 和 SDL2 开发包，用于桌面模拟器。
- 与当前 `lpm.toml` schema 兼容的 LPM，并且 `lpm` 在 `PATH` 中可用。
- 设备构建需要网络以首次下载已锁定且校验过 SHA-256 的 AArch64 BSP。

开发前先初始化仓库锁定的 AppKit submodule：

```sh
git submodule update --init --recursive
```

默认 preset 将 `LilyGoUI_DIR` 指向 `third_party/cm0-appkit`。应用通过
`find_package(LilyGoUI CONFIG REQUIRED)` 加载源码 SDK 配置，由 SDK 构建
AppKit/LVGL 静态库并链接进应用。应用 CMake 不得枚举 SDK 私有源文件或引用
其私有头文件。AppKit 代码更新后需要更新子模块版本并重新构建应用。

`lilygo-ui-appkit-dev` 0.1.0 或更新版本同时提供源码 SDK、公共字体和许可证，
应用不重复打包字体。`lpm.toml` 的 `min_appkit_version` 指定该软件包的最低版本。
AppKit/LVGL 仍静态链接进每个应用，该软件包不包含 AppKit/LVGL 动态库。
交叉编译 sysroot 只需 BSP 和系统开发依赖，不需要预装 AppKit SDK。
主机预览使用源码 SDK 中的字体资源。

## 配置与元数据

`lpm.toml` 是唯一由开发者维护的项目配置和产品元数据源，包含：

- 应用标识、版本、标题、描述、许可证和作者；
- 图标、预览图、权限、兼容性和 Launcher 排序；
- 主机、交叉构建 preset 和包输出目录；
- 部署主机、用户和端口默认值。

CMake 在配置阶段通过 LPM 读取并校验 `lpm.toml`，然后在构建目录中生成 CMake
变量、应用身份头文件、Launcher manifest、desktop 文件和 AppStream 元数据。
`data/*.in` 只能保留通用模板，不得重复写入专属于应用的值。

Keyboard 页面提供按键震动强度设置，设置页同时保留 Wi-Fi、Bluetooth、Ethernet、Cellular、Battery、General 和 About 页面。

Wi-Fi 页面以 iOS 风格的圆角分组展示当前网络、已知网络和其他网络。已知网络来自
NetworkManager 保存在设备上的连接配置，离开覆盖范围后仍保留，点击后按 UUID 重连，
无需再次输入已保存的密码。应用不读取或另存密码。点击当前连接或信息按钮进入详情，
通过 TCP/IP、DNS、Hardware 三个标签查看 IPv4/IPv6 配置、地址、子网掩码、路由器、
DNS 服务器、搜索域、接口、MAC 和 MTU。详情为只读；缺失数据以 `--` 表示。
忽略网络必须再次确认，成功后才移除所选连接配置；授权失败会显示错误。加入和忽略
网络通过 `sudo -n` 执行，完整 Debian 包提供面向现有管理员用户的有限 Wi-Fi 命令
授权；密码通过标准输入传递，不出现在进程参数中。Wi-Fi 开关沿用平台原有权限。

Ethernet 页面内联显示 IPv4 配置模式下拉框，不使用独立配置弹窗，可选择 DHCP 或手动。DHCP 模式下
IP Address、Subnet Mask、Gateway 不可修改；手动模式下启用这些字段，DNS 在两种模式下
均可编辑。输入会在本地校验，保存时更新 NetworkManager 连接并重新激活；目标系统需要
为运行应用的管理员用户安装应用提供的有限 `nmcli` sudoers 权限。

Cellular 页面通过 `/dev/ttyACM0` 直接访问 Nordic nRF9151 Serial LTE Modem（SLM），
串口配置为 115200 baud、8 data bits、无校验、1 stop bit。确认模组支持后，使用
`AT+CFUN=21` / `AT+CFUN=20` 控制 LTE 使能并保留 GNSS 状态，操作后回读功能模式和
系统模式。页面不修改 APN 或激活 PDP 上下文，实际联网仍取决于模组配置、SIM 和网络。

SIM 状态、ICCID、IMSI、运营商、注册状态、LTE-M / NB-IoT 制式和 RSRP 信号强度分别
通过 `AT+CPIN?`、`AT%XICCID`、`AT+CIMI`、`AT+COPS?`、`AT+CEREG?` 和 `AT+CESQ`
读取。串口不存在、权限不足、被占用、缺少 SIM、需要 PIN / PUK 时明确显示状态。
流量通过可选的只读命令 `AT%XCONNSTAT?` 读取模组采集期间的收发统计，原始单位为 kB，
界面使用十进制单位。Settings 不启动、停止或清零采集；采集已停止时，返回值可能不再
更新。这些值不是跨重启持久化统计、月度账单或套餐余量，命令不支持或读取失败时显示
不可用，不从主机 Wi-Fi 或以太网接口推测蜂窝流量。

运行应用的用户需要由系统授予串口设备读写权限，例如加入设备所属串口组，或配置适当的
udev 规则。蜂窝页面不依赖 NetworkManager、ModemManager 或 sudo。默认串口与独立的
`lilygo-ui-cellular` 应用共用 `/tmp/lilygo_nrf9151_uart.lock`，使用前需关闭另一应用的
模组会话。`LILYGO_UI_CELLULAR_DEVICE` 可覆盖串口路径，用于平台集成与 PTY 测试。
协议依据 [nRF91x1 Cellular AT Commands](https://docs.nordicsemi.com/r/bundle/ref_at_commands_nrf91x1/)。

General → Sharing 提供 Remote Login 开关，通过 systemd 启停 SSH 并同步开机启动设置。
关闭时同时停止已安装的 ssh.socket，防止 socket 再次激活服务。页面异步查询实际状态，
操作期间禁用开关，失败后重新查询并显示错误。目标系统须安装 OpenSSH server，并为
运行应用的用户提供管理相应 systemd 单元的权限；应用不自行授予权限或修改 SSH 认证。
如果平台启动应用时设置 Linux `no_new_privs`，则 `sudo` 无法提权，平台需要提供等效的
受控特权 broker 才能执行该操作。

Sharing 的 Serial Console 开关通过 `raspi-config nonint get_serial_cons` 和
`do_serial_cons 0/1` 管理串口控制台，不改变独立的 UART 硬件设置。保存并回读成功后
弹出 Restart Now / Restart Later；只有用户选择立即重启时才请求 systemd 重启。
稍后重启保留配置，通过对比 `/proc/cmdline` 显示待重启状态，重新打开应用仍能识别。
保存失败不弹出重启确认；重启失败保留弹窗并允许重试或稍后重启。

Battery 页面分别识别 BQ27220 电量计和 BQ25890 充电器，显示剩余百分比、健康状态、
剩余/满充/设计容量、循环次数及实时电压、电流、温度。Maximum Capacity 为满充容量与
设计容量的比值，不伪装成芯片直接报告的 SOH 百分比。

Battery Calibration 可编辑设计容量及可见满充容量（mAh），先确认具体数值，再保存并
回读。目标板原有驱动不支持参数写入，必须先应用 `data/kernel/` 的配套 BQ27220 补丁；
旧驱动显示升级提示且禁用保存。自定义 `calibration_capacity` 接口一次接收两个值，
驱动负责配置更新、校验、估算重初始化和安全状态恢复。参数保存在电量计 RAM 中，
电量计断电可能丢失；应用不写 OTP、不替换 CEDV 化学模型，也不在每次启动时覆盖学习结果。
可选的充放电引导只跟踪过程，不宣称驱动无法验证的“学习成功”。

BQ25890 提供输入限流与充电电流设置，使用芯片支持的步长，充电电流上限来自设备树。
滑块先产生草稿，Apply 才写入并回读；输入限流应匹配电源和线缆能力。应用不把 Hi-Z
输入控制误标为充电开关，也不修改充电电压。普通管理员用户通过完整包内的固定功能
helper 和有限 sudoers 规则执行操作，页面保持响应；测试路径不会传入提权 helper。

设备连接实体键盘后，AppKit 会把 Settings 的菜单项、返回按钮、开关、滑块和文本框
加入焦点链。`Tab` 和 `Shift+Tab` 切换焦点，`Enter` 激活当前控件，`Esc` 取消文本
输入或退出应用；字符按键输入到当前文本框。实体键盘可用时隐藏软件键盘，拔出后不再
抑制软件键盘。

Settings 的稳定公开身份是：

```text
包名和可执行文件  lilygo-ui-settings
应用 ID            cc.lilygo.ui.Settings
Desktop/AppStream  cc.lilygo.ui.Settings
```

`publish.json` 是 LPM 发布流程中的临时派生数据，不是项目配置入口，不得手工编辑或
提交。项目源码、CMake 和文档不得依赖该临时文件存在。

## 工作流程

LPM 是开发者的首选入口，CMake presets 是可独立调用的底层稳定接口。

| 目标 | LPM 入口 | CMake 底层入口 | 输出 |
| --- | --- | --- | --- |
| 启动桌面模拟器 | `lpm start` | `cmake --preset host-simulator` 后构建 | `build/host-simulator/` |
| 运行主机测试 | `lpm test` | `ctest --preset host-simulator` | 测试结果和渲染快照 |
| 交叉构建和打包 | `lpm pack` | `cmake --preset cm0-cross` 后运行 CPack | `dist/*.deb` |
| 部署到设备 | `lpm deploy` | 无 | 设备上已安装的 Debian 包 |

不得绕过 presets 依赖 `build/` 内部目录结构，也不得在开发机上进行源码内构建。

## 目录结构

```text
lilygo-ui-settings/
├── assets/
│   ├── app-icon.png                 # 必须，128x128 RGBA 应用图标
│   └── previews/                    # 发布预览图，不进入 deb
├── cmake/
│   ├── LpmMetadata.cmake           # 读取并转换 lpm.toml
│   ├── app_identity.h.in           # 应用身份头文件模板
│   └── cm0-bsp-toolchain.cmake     # 设备交叉工具链入口
├── data/                            # Launcher/Desktop/AppStream 通用模板
├── docs/                            # 架构、UI 和产品设计文档
├── src/                             # 应用和页面实现
├── tests/                           # 单元、绑定和无头响应式渲染测试
├── third_party/
│   └── cm0-appkit/                 # 已锁定的 SDK Git submodule
├── .clang-format
├── AGENTS.md
├── CMakeLists.txt                  # 顶层构建、安装和 CPack 规则
├── CMakePresets.json              # 稳定的主机和设备构建入口
├── lpm.toml                       # 唯一开发者维护的项目元数据源
└── README.md                      # 面向使用者的快速开始
```

`images/`、`audio/` 和 `fonts/` 是按需目录，无资源时不必保留空目录。应用
不得复制或再打包 AppKit 提供的 Inter、Source Han Sans CN 或 Font Awesome 完整
字体。`fonts/` 只用于 AppKit 运行时字体不包含的应用专用字体。

## 源码边界

- 每个页面位于独立的 `src/pages/<page>/` 目录，页面私有的 Model、ViewModel
  和 View 放在一起。
- Model 保存业务状态和领域规则，不得依赖 LVGL。只有当业务对象被多个页面
  实际共享时，才将它提升到 `src/domain/`。
- ViewModel 将 Model 转换为可观察的展示状态并提供用户命令；不得持有具体页面
  widget 的所有权。
- View 只负责 LVGL widget 创建、布局、事件转发和状态绑定；不得直接修改
  Model。
- 全局导航状态由 `app.cpp` 或按需新增的 `app_router.cpp/.hpp` 管理，不得由页面
  View 持有。
- `components/` 只放置至少被两个页面使用、或明确承担公共绑定和资源生命周期的
  组件。

数据流向固定为：

```text
用户输入 -> View 事件 -> ViewModel 命令 -> Model 更新
         -> 可观察状态 -> View 绑定 -> LVGL 渲染
```

## 生成物与提交边界

以下目录是本地状态或构建产物，不属于源码目录，不得提交：

```text
.cache/           # 已校验的 BSP 等下载缓存
.lpm/             # LPM 运行状态和报告
.venv/            # 本地 Python 环境
build/            # CMake 构建树和测试渲染产物
dist/             # Debian 包及打包临时产物
_CPack_Packages/  # CPack 临时工作树
```

项目不预设 `packaging/` 目录。Debian 安装和打包规则应优先保持在顶层
`CMakeLists.txt` 和 `data/` 模板中；只有出现 CPack 无法表达的真实打包文件时，
才按需创建该目录。

## 验收要求

每次提交至少需要完成主机配置、构建和测试：

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
```

修改 UI 时必须检查竖屏和横屏渲染结果，确认文本无缺字、重叠和裁切，主要
操作始终可见且可达。修改安装、SDK、工具链、元数据或打包逻辑时，还必须完成
`cm0-cross` 构建和 Debian 打包验证。

新应用不得声明 Debian `Provides`、`Conflicts` 或 `Replaces`。本项目只为迁移真实
已发布的 `lilygo-cm0-settings` 保留这三个关系；不得据此添加其他旧命名。
