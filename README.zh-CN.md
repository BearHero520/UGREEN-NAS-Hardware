# ugreenctl

ugreenctl 是一个面向绿联 NAS 的开源、用户态、插件化硬件管理工具。

[English](README.md) · [兼容性目录](docs/COMPATIBILITY.zh-CN.md) ·
[Compatibility catalog](docs/COMPATIBILITY.md)

它按照 NAS 机型加载独立的 .so 插件。只有硬件控制器、寄存器映射和写入行为均已验证
的机型才会开放写入能力；models/ 中存在某机型的插件，不代表该机型已被支持。

## 当前支持情况

| 机型插件 | 状态 | 已验证能力 |
| --- | --- | --- |
| dxp4800plus | 已支持 | CPU/系统风扇状态与 PWM、交流电恢复后的启动策略 |
| dxp480tplus | 固件逆向完成 | CPU、sysfan1、sysfan2 转速；CPU/全部风扇 PWM 与来电启动策略需 --force --apply |
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
    sudo ugreenctl --apply power startup set restore

默认所有写命令都只是预览；只有带上 --apply 才会写硬件。--force 会跳过 DMI
匹配、原厂驱动冲突、IT8613 芯片 ID 和最低 PWM 的安全检查，仅供充分验证硬件后的
调试使用。

DXP480T Plus 的写入路径已从原厂固件中恢复，但尚未完成实机验证，因此写命令额外要求
--force。插件仍会保留原厂驱动冲突和 IT8613 芯片身份检查：

    sudo ugreenctl --force --apply fan set cpu 120
    sudo ugreenctl --force --apply fan set all 120

all 对应原厂 set 命令，按原厂顺序写入已观察到的 3 个 PWM 通道。在实机确认系统风扇
转速与 PWM 通道的逐一对应关系前，插件不会开放单独的系统风扇写操作。

## 安全说明

- DXP4800 Plus 插件发现原厂 /proc/it86 驱动仍在运行时会拒绝访问，避免并发操作
  寄存器。
- 每次控制器访问都会使用 /run/ugreenctl-it8613.lock 进行进程锁定。
- 直接端口 I/O 需要 root 或 CAP_SYS_RAWIO。
- 手动 PWM 会关闭对应风扇通道的自动模式；请勿设置过低 PWM，也不要在没有独立温度
  监控时使用 --force。

如果你在原厂 NAS 固件之外运行该工具，且原厂模块已加载，先卸载它：

    sudo modprobe -r ug_it86x_cpufan

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

每个 models/*.so 都需要导出 ABI version 2 的入口：

    const struct ugreenctl_plugin *ugreenctl_plugin_v2(void);

ABI 定义在 include/ugreenctl.h。核心程序只接受 ABI version 2 的插件，并通过
DMI 产品名或 --model 选择机型。

## 来源与许可

DXP4800 Plus 的映射是根据绿联系统固件 1.17.0.95 及其
ug_it86x-cpufan.ko 的可观察行为进行的独立、用户态复现。项目不链接也不分发
任何原厂驱动源码或二进制。
