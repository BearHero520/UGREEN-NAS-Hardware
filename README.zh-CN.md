# ugreenctl

ugreenctl 是一个面向绿联 NAS 的开源、用户态、插件化硬件管理工具。

[English](README.md) · [兼容性目录](docs/COMPATIBILITY.zh-CN.md) ·
[Compatibility catalog](docs/COMPATIBILITY.md)

它按照 NAS 机型加载独立的 .so 插件。已实机验证的机型开放正常写入；仅完成固件逆向
的机型只能在保留全部保护并明确确认风险时开放受限写入。models/ 中存在某机型的插件，
不代表该机型已被支持。

## 当前支持情况

| 机型插件 | 状态 | 已验证能力 |
| --- | --- | --- |
| dxp4800plus | 已支持 | CPU/系统风扇状态与 PWM、交流电恢复后的启动策略 |
| dxp4800s | 固件逆向完成 | sysfan1 转速；系统风扇 PWM 64-255 与来电启动策略需 --force --apply |
| dxp480tplus | 已支持 | CPU、sysfan1、sysfan2 转速；hwmon CPU/全部风扇 PWM、来电启动策略；受保护的共享 PWM 直控兜底 |
| dxp2800、dxp4800、dxp6800pro、dxp8800plus | 仅机型档案 | 暂无 |

dxp4800plus 同时匹配 DMI 产品名 DXP4800 Plus 和 DXP4800 Pro。该插件针对在
固件 1.17.0.95 中观察到的 ITE IT8613 Super I/O 硬件监控器。

## 构建和安装

在 x86/x86_64 Linux（例如 NAS 的 SSH 终端）中执行：

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure
    sudo cmake --install build

也可以使用一键安装脚本：

    sh ./scripts/install.sh --install-deps

该选项会通过系统包管理器安装 CMake 和 C 编译器，再构建并安装到 /usr/local。
如果依赖已存在，直接运行：

    sh ./scripts/install.sh

## 直接运行（不安装）

    sh ./scripts/run.sh models
    sudo sh ./scripts/run.sh info
    sh ./scripts/run.sh fan set cpu 120
    sudo sh ./scripts/run.sh --apply fan set cpu 120

run.sh 会在 build/ 中构建程序，并自动传入正确的插件目录。

## 常用命令

    ugreenctl models
    sudo ugreenctl info
    sudo ugreenctl fan status
    ugreenctl fan set cpu 120
    sudo ugreenctl --apply fan set cpu 120
    sudo ugreenctl --apply fan mode cpu auto
    sudo ugreenctl --apply power startup set restore

默认所有写命令都只是预览；只有带上 --apply 才会写硬件。`--force` 只表示确认使用
尚未实机验证的逆向写入路径，不会绕过精确 DMI、原厂驱动冲突或 IT8613 芯片 ID 检查。

DXP4800S 只有一个已恢复的 `sysfan1` 通道，插件命名为 `sys`。当前 PWM 和模式保持
`unknown`，普通写入只开放原厂曲线已经使用的 `64..255`：

    sudo ugreenctl --force --apply fan set sys 120
    sudo ugreenctl --force --apply power startup set restore

原厂自动模式由用户态 `hwmonitor` 根据温度写 PWM，不是 IT8613 硬件自动模式。当前工具
提供受保护的手动写入原语；其他系统若要自动调速，仍需独立的温度守护与故障保护。

DXP480T Plus 的固件逆向写入路径已经完成实机验证。正常写入只需 --apply，并继续保留
精确 DMI 匹配、原厂驱动冲突、IT8613 芯片身份和最低 PWM 检查：

    sudo ugreenctl --apply fan set cpu 120
    sudo ugreenctl --apply fan set all 120

hwmon 存在时，all 对应原厂 set 命令，按原厂顺序写入 3 个 PWM 通道。若为释放通道而
卸载 it87、`name=it8613` hwmon 节点不存在，DXP480T Plus 的 cpu 与 all 会复现原厂
共享直控输出：控制寄存器 `0x17`、占空比寄存器 `0x73`。三路转速仍会报告，但 sysfan2
没有固件证明的独立 PWM 路径，因此 PWM/模式显示为 unknown；插件不会开放单独的系统
风扇写操作。该共享直控 all 路径仍等待实机验证，且始终需要 `--force --apply`、原厂接口
与 it87 冲突检查、芯片身份检查和进程锁。

## 安全说明

- 所有 IT8613 插件发现原厂 `/proc/it86` 驱动仍在运行时都会拒绝访问，避免并发操作
  寄存器。
- 每次控制器访问都会使用 /run/ugreenctl-it8613.lock 进行进程锁定。
- 直接端口 I/O 需要 root 或 CAP_SYS_RAWIO。
- 手动 PWM 会关闭对应通道的硬件自动位。DXP4800S 的原厂自动控制是软件守护进程，
  手动接管前必须停止或替代原厂守护，并保持独立温度监控。

如果你在原厂 NAS 固件之外运行该工具，且原厂模块已加载，先卸载它：

    sudo modprobe -r ug_it86x_sio       # DXP4800S / DXP4800 分支
    sudo modprobe -r ug_it86x_cpufan    # DXP4800 Plus / DXP480T 分支

## 后续增加机型

机型兼容性以 [models/compatibility.json](models/compatibility.json) 为发布目录，
每个机型都必须记录：

- 固定的插件 ID 和精确 DMI 产品名；
- 支持等级：仅机型档案、只读或已支持；
- 已验证能力、控制器和固件版本；
- 已知冲突、限制条件和可复现实机证据。

新增机型前请使用 [新机型接入模板](docs/NEW_MODEL_TEMPLATE.md)。禁止因为外观、
CPU 或主板“看起来相近”就复用另一个机型的写寄存器映射。

## 插件 ABI

需要风扇模式控制的机型可导出 ABI version 3，同时保留 ABI version 2 兼容入口：

    const struct ugreenctl_plugin *ugreenctl_plugin_v3(void);

    const struct ugreenctl_plugin *ugreenctl_plugin_v2(void);

ABI 定义在 include/ugreenctl.h。核心程序优先加载 ABI version 3，无法找到时回退到
ABI version 2，并通过 DMI 产品名或 --model 选择机型。

## 来源与许可

DXP4800 Plus 的映射是根据绿联系统固件 1.17.0.95 及其
ug_it86x-cpufan.ko 的可观察行为进行的独立、用户态复现。项目不链接也不分发
任何原厂驱动源码或二进制。

DXP4800S 的证据和未验证项见
[DXP4800S 逆向记录](docs/DXP4800S_REVERSE_ENGINEERING.zh-CN.md)。在补充实机验证记录前，
其支持等级保持为 `reverse-engineered`。
