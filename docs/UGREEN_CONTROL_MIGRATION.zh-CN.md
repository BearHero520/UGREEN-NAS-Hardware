# UGreen Control 迁移与主线合并

2026-10-28，配套应用从 LLLED_FPK 的 `almighty` 迁移至 [UGreen-Control/main](https://github.com/BearHero520/UGreen-Control/tree/main)。LLLED_FPK 的 `almighty` 可以并入其 `main`，并发布 v3.0.5 迁移包；应用内后续更新及 README 指向新仓库。

本仓库实际分支名为 `Almighty`（区分大小写），可以并入 `main`；本次将该分支及当前独立 WOL/RTC、DXP6800 与兼容温控实现合入主线。硬件能力与安全保护仍在本仓库实现，应用只提供适配、API 和 UI。此次迁移不新增硬件控制后端。

应用固定使用 `artifacts/Almighty/2026-10-28-dxp6800-wake-curve-local/` 的不可变预览包及其 SHA-256。目录名称保留生成时的来源，不代表已经通过全部机型实测；归档内源码哈希与基包记录是二进制来源依据，承载归档的 Git 提交不是整包源码的统一构建版本。

本次重新配置、构建并运行 Linux x86_64 隔离 QEMU 测试，33 项全部通过；未向 NAS 硬件写入，未新增实机验证。DXP6800 原厂曲线仍未知，兼容预设使用 Pro 参数。
