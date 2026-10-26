# N76 手动触发编译

`ugreenctl-n76` 是可选的 Python 3.10+ 构建入口。管理员手动执行后，识别本机
精确 DMI 和当前内核，取得固定提交、固定 SHA-256 的官方源码，应用本项目的安全
补丁并调用 Kbuild。默认仅检查和预览；只有 `build --apply` 下载源码和编译。
它不安装依赖、不安装模块、不加载模块、不创建 I2C client，也不修改启动项。

当前支持选择 `DH2600` 和 `DXP480T Plus`，均为 x86_64。其它机型不会套用 N76
驱动。现有 LED sysfs 后端及其模块绑定检查保持不变；编译成功不会将两款机型升级
为无驱动直控或实机验证通过。

## 使用

源码工作树中使用：

```sh
python3 scripts/ugreenctl-n76.py check
python3 scripts/ugreenctl-n76.py build --apply --work-dir /mnt/pool/ugreenctl-n76
```

通过 CMake 安装后，入口是 `ugreenctl-n76`；完整工具包中的入口位于
`usr/local/bin/ugreenctl-n76`，资源位于 `usr/local/share/ugreenctl/n76/`。
保留相对目录关系，可直接使用 `python3 /绝对路径/ugreenctl-n76 check`。

```sh
ugreenctl-n76 check
ugreenctl-n76 build --apply --work-dir /mnt/pool/ugreenctl-n76
# 已缓存全部三份经校验的官方源码时，可断网重编译：
ugreenctl-n76 build --apply --offline --work-dir /mnt/pool/ugreenctl-n76
```

`check` 和省略 `--apply` 的 `build` 不联网、不创建目录、不运行 make。
JSON 返回缺少的构建前提；不满足时退出码为 1。工作目录必须属于执行用户且权限为
0700，新目录自动按该权限创建。构建通常不需要 root；安装和加载没有包含在此命令中。
Kbuild 会将目录插入 make 语法和 shell 命令，因此内核目录和工作目录只接受 ASCII
字母、数字、`/ . _ + -`，不能含空格、中文或 shell/make 特殊字符。

需要当前内核对应的完整 prepared build tree：默认
`/lib/modules/$(uname -r)/build`，也可通过绝对路径 `--kernel-build` 指定，仍要求
其 `kernel.release` 与 `utsrelease.h` 都精确匹配当前运行内核。要求
`CONFIG_MODULES=y`、非空 `Module.symvers`、生成的配置文件、可执行的 fixdep/modpost
及配置要求的 objtool。GCC 内核使用系统 GCC；Clang 内核选择 LLVM 工具链。
其它辅助工具及其库依赖由 Kbuild 检查，缺少时保留失败日志。
默认并行数 2，`--jobs` 范围 1..16。单次编译上限 30 分钟，超时或中断会终止该
编译进程组。不会自动执行包管理器或修改系统开发模式。

TrueNAS SCALE、fnOS、PVE 和 Unraid 的定制内核必须分别取得匹配的头文件、配置和
符号文件；没有这些材料时会明确失败。标准 Debian/Ubuntu 也须使用当前内核对应的
材料。**“自动编译”不表示自动解决任意发行版缺失的内核开发包。** 系统升级后需再
手动执行；编译成功也不证明模块签名、加载策略和运行时符号全部满足目标系统要求。

## 源码与安全边界

固定上游：UGREEN `kernel-6.12` 提交
`c75c5abd60dd45f45966720293ce1ee505fb8615`。

| 输入 | SHA-256 |
| --- | --- |
| DXP480T Plus `leds-mcu-n76e003.c` | `352bd2d58053e57122eaad4fe9e1c671e9447eb1c8937f87951665a804f4ed7a` |
| DH2600 `leds-mcu-n76e003-dh2600.c` | `1ea3550c7a44d1918859eb6df10dcd5fba30ccf8c64228c42ee83978a17c5ba5` |
| `ledtrig-normal-n76e003.c` | `0249ed503f4cc9e8e1203c27b26b11f3176be833ba4e0a2070d28efb45251183` |
| `ledtrig-breath-n76e003.c` | `d3da0de5fe25914ce507ffd34ba6b9ae25392f97bc9da964bfc4a84fb850b29f` |

每个机型只取自身主驱动和两个 trigger。URL 和来源位于
`tools/n76/sources.json`；原始缓存保留不改写，SHA 不符直接拒绝。Git 和工具包只
保存本项目编写的准备脚本、清单和安全覆盖头文件，不带原厂固件或预编译 `.ko`。
生成的驱动和 trigger 保留上游 GPL 标识；安全覆盖头文件为 GPL-2.0-or-later。

原厂主驱动的精确机型检查被关闭，且手动创建 I2C client 可以跳过 `detect()`。
准备脚本在 `probe()` 第一次初始化写入前重新检查精确 DMI、7 位地址
`0x31/0x26`、SMBus byte-data 能力、芯片 `0x5a/0x5b == 0xa5b5` 和可读取的
`0x5d` MCU 版本；读错误保留负 errno，拒绝第二个 client。移除广泛地址扫描，
只保留后续显式绑定的入口。两种主驱动都注册 `mcu_led`，不能同时使用。
触发器也按精确 DMI 限定，注册冲突正常报错。清理先阻止调度并撤销 LED，再等待
工作项结束，避免原厂 remove 中持锁等待 worker 的风险。

**加载后显式绑定到硬件会执行初始化写入。** 本次仅提供构建，不提供自动安装或
绑定，更不使用 I2C_FORCE。白/橙/组合色、DH2600 二值亮度、480T 仅电源灯等能力
边界不变；颜色、亮度、时序的驱动缓存仍不等于硬件真实回读，详见
`ALL_MODELS_LED_EVIDENCE.zh-CN.md`。

## 产物与验证

每次生成独立的 `<kernel-release>-<随机后缀>/`：`source/` 保存打补丁源码、
Kbuild、来源记录和 `.ko`；`build.log` 保存编译输出；`result.json` 保存命令、
来源 SHA、每个模块的 SHA、ELF 架构、名称、许可证及 vermagic。失败保留现场，
不会覆盖历史构建。输出会静态检查为 x86_64 ELF relocatable、精确预期三模块和
匹配当前内核的 vermagic；不会执行产物。

测试包含：无 apply 零下载/零编译、头文件版本不匹配拒绝、环境覆盖清理、错误源码
SHA/FIFO/链接拒绝、模块 ELF/名称/vermagic 检查，以及身份改变和编译失败的状态
记录。真实 Kbuild 验证与实机加载是不同层次；当前无 DH2600 或 DXP480T Plus 的
实际加载和灯光验证记录，不能由编译通过推定整机适配完成。

本次实际编译使用官方 prepared 6.18.15 头文件包（SHA-256
`8c975501c942b1d9b4aca779038bdee9ada1a5514ed9800b6cdd160b5a7608a7`），
在隔离 RAM-only QEMU 的 Debian GCC 12.2.0 / binutils 2.40 / make 4.3 环境中，
两款各三模块全部 Kbuild 和严格 MODPOST 通过。独立 ELF 检查确认名称、许可证、
`6.18.15` vermagic 和未定义符号与 `Module.symvers` 一致。该配置未启用
CONFIG_MODVERSIONS；头文件包不带 vmlinux，Kbuild 跳过可选 BTF。
VM 运行内核为 6.12.110，故这是针对 6.18.15 的外部模块交叉构建证明，并非手动
入口在真实 NAS 上对当前内核检查和加载的端到端证明。其他内核版本仍待构建验证。
证据保存在 `firmware-inbox/work/almighty-n76-manual-build-20261026/review/`，
没有将编译器、头文件包或生成模块放入 Git 和发布包。

构建前提参考 [Linux 外部模块构建文档](https://docs.kernel.org/kbuild/modules.html)。
