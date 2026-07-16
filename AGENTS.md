# reference-hardware 项目记忆与执行约定

本文件适用于 `E:\工作台\4800plus\reference-hardware`。同时继承上级 `E:\工作台\4800plus\AGENTS.md`；冲突时以本文件中更具体的项目约定为准。

## 项目定位

- 本仓库是 `ugreenctl`：Linux x86/x86_64 用户态、插件化的 UGREEN NAS 硬件控制工具。
- 机型目录以 `models/compatibility.json` 为发布状态的主要来源，以 `docs/HARDWARE_SUPPORT.md` 和对应逆向文档作为证据说明。
- `models/*.c` 只负责精确 DMI 匹配和能力接线；寄存器操作应放在 `fan/`、`power/`、`superio/`、`led/` 或其他对应硬件层。
- 原厂镜像和提取物不得提交进仓库。临时输入放 `firmware-inbox/input/`，临时解包结果放 `firmware-inbox/work/`。

## 已知机型记忆

### DXP4800 Plus / DXP4800 Pro

这是已确认的 IT8613 Super I/O 方案，不是 ACPI EC：

- 精确 DMI：`DXP4800 Plus`、`DXP4800 Pro`
- Super I/O 配置端口：`0x2e / 0x2f`
- Hardware Monitor 端口：`0xa35 / 0xa36`
- CPU 风扇控制/PWM：`0x16 / 0x6b`
- 系统风扇控制/PWM：`0x17 / 0x73`
- 来电启动策略寄存器：`0xf2 / 0xf4`
- 证据固件：UGREEN `1.17.0.95`
- 实现方式：Linux 用户态端口 I/O，不依赖原厂内核模块

这些数值只适用于有证据的精确机型，不能自动套用到 DXP4800、DXP4800S、DXP480T Plus 或其他外观相似设备。

### 其他机型

- DXP480T Plus：查看 `docs/HARDWARE_SUPPORT.md` 和 `fan/it8613_dxp480t.*`；区分固件逆向与实机验证状态。
- DXP4800S：查看 `docs/DXP4800S_REVERSE_ENGINEERING.zh-CN.md`；不得因为名称接近而使用 DXP4800 Plus 的双风扇映射。
- 每次开始工作前读取当前 `git status`，因为仓库可能保留用户尚未提交的逆向或实现改动。

## 新系统包的默认处理方式

用户要求逆向新包时，不假定包结构固定，按 `docs/FIRMWARE_REVERSE_PLAYBOOK.zh-CN.md` 的问题树执行：

1. 先明确目标能力，例如风扇、LED、BIOS/来电启动、蜂鸣器或系统信息。
2. 对输入只读取证：文件名、大小、SHA-256、magic、容器层级、OS/Build/Kernel。
3. 根据实际格式逐层拆解，不把上一个固件的命令机械复用到新包。
4. 先找机型路由脚本、配置和服务，再定位对应内核模块或用户态二进制。
5. 对关键 `.ko` 检查字符串、符号、重定位、静态数据和关键函数反汇编。
6. 回答“谁在控制、通过什么总线、读写入口在哪里、寄存器是什么、谁会冲突、自动模式由谁实现”。
7. 把结论分为固件证据、静态推断、实机验证和 unknown。
8. 只有证据足够时才修改代码；先加入精确 DMI 和安全检查，再逐项开放能力。

如果 `firmware-inbox/input/` 只有一个明显的系统包，可直接分析；存在多个候选且用户未指定时，先列出候选并询问目标文件。永远不修改或删除 `input/` 中的原文件。

## 逆向产物位置

- 临时拆包：`firmware-inbox/work/<包名或机型>/`
- 可长期保留的方法和结论：`docs/`
- 新机型证据记录：复制并填写 `docs/NEW_MODEL_TEMPLATE.md`
- 兼容状态：`models/compatibility.json`
- 实现代码：按硬件层放入对应目录，不把全部逻辑塞进机型插件

临时目录可以很大，默认不清理；只有用户明确要求时才删除。

## 实现和验证要求

- 写寄存器前必须精确匹配 DMI、确认芯片 ID、检查原厂驱动冲突并使用进程锁。
- `--force` 不能成为绕过芯片身份和并发冲突保护的理由。
- 风扇写入保持安全下限；当前 PWM 不可读时如实返回未知。
- 自动调速若来自原厂软件守护进程，应在移植时实现软件曲线，不得误称为硬件自动模式。
- 修改后至少运行：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- 未经用户明确要求，不提交、不推送，也不修改 `E:\工作台\LLLED_FPK`。

## 交付格式

每次逆向或接入完成后，回复必须说明：

- 输入包及 SHA-256；
- 找到的机型分支和控制器；
- 已确认的接口/寄存器及证据位置；
- 哪些只是静态推断或 unknown；
- 提取目录和新增文档；
- 构建/测试结果；
- 是否修改了 `LLLED_FPK`。
