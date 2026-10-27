# Almighty N76 直控预览包（2026-10-27）

源码提交 `aff7991ee58709ad34b5afbc2787f0d21300368d`。仅用于 Almighty 分支评审，尚未合并 main，不是完整25款验收发布。

| 架构 | 工具包 |
| --- | --- |
| x86_64 | [下载](ugreenctl-almighty-0.2.0-linux-x86_64-preview.tar.gz) |
| ARM64 | [下载](ugreenctl-almighty-0.2.0-linux-aarch64-preview.tar.gz) |

[SHA256SUMS](SHA256SUMS) 校验两份工具包及各自 manifest；包内每个文件还有独立 SHA-256。
包含四个程序、九个机型插件、证据/兼容文档和本轮完整测试日志。**不再包含 N76 驱动编译辅助工具**，
没有 `.ko`、原厂源码、固件、运行库或编译器，不安装或启动服务。

DH2600 / DXP480T Plus 新增显式 `--n76-direct BUS:0x31`（或 `BUS:0x26`）用户态 I²C 直控。
必须填写本机已经确认的总线/地址；不扫描、不猜测。使用完整的 `led set ... --mode ... --color ...`。
默认 dry-run，实写需 `--force --apply`。模式寄存器回读，实际灯色/开关/PWM/时序仍为 unknown；
成功输出 `sent-unverified`，不宣称真实灯态已验证。不支持可恢复的 identify/locate 或任意呼吸周期。

不指定直控选项时，保留原有已绑定驱动的 sysfs 方式。Linux 仍须提供 I²C 总线驱动及 i2c-dev。
详见 [直控说明](../../../docs/N76_DIRECT_CONTROL.zh-CN.md)。

两个架构 Release 配置、构建、完整 CTest **26/26**；另检查实际包内文件和插件加载。
没有 DH2600/DXP480T Plus 实机验证，ARM 包不意味着这两款存在 ARM 硬件版本。
TrueNAS 指 Linux/SCALE，非 FreeBSD/CORE。工具依赖动态 glibc，具体 ABI/依赖见 manifest。
8款 M.2 物理槽位、GT SATA/BIOS 路由及真实硬件验收等缺口仍保留。
旧 `2026-10-27-f2ffb71` 包含已撤回的驱动编译方案，不代表当前版本。
