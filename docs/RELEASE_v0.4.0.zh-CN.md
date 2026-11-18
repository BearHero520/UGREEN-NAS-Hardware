# UGREEN NAS Hardware v0.4.0

- 新增 iDX6012 双风扇同步调速、设备智能模式恢复、软件温控、来电开关和双网口 WOL。指定主板已有调速和设置回读记录，实际断电恢复与关机唤醒待验证。
- 新增 DXP4800 GT 手动调速、原厂八点软件温控及实验性来电开关。改进 EC 占用诊断，并提供磁盘物理槽位 TSV 输出。目标 NAS 实机验证待完成。
- 接入 DXP8800 Pro 和 DXP2800 EM26 的风扇及来电恢复能力。WOL 新增物理网卡枚举。

Linux x86_64 和 ARM64 归档各通过 42 项 CTest，包含 13 个机型插件。ARM64 不提供 x86 端口 I/O 能力。下载归档后请用 SHA256SUMS 校验。此次软件发行保留各项硬件能力的实验性标记。

[源码提交](https://github.com/BearHero520/UGREEN-NAS-Hardware/commit/a3cbdfbb132b070f19715e0ef978f00e17e0bb08)
