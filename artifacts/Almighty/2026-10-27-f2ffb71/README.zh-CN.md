# Almighty 预览工具包 · 2026-10-27

构建源码提交：[`f2ffb7150648e101bb3246f8949c9ba6d40a50c2`](https://github.com/BearHero520/UGREEN-NAS-Hardware/commit/f2ffb7150648e101bb3246f8949c9ba6d40a50c2)。
这些文件直接保存在 `Almighty` 分支，未合并到 `main`，没有新建 GitHub Release。

| 架构 | 工具包 | 文件清单 |
| --- | --- | --- |
| Intel / AMD x86_64 | [x86_64 tar.gz](ugreenctl-almighty-0.2.0-linux-x86_64-preview.tar.gz) | [manifest](ugreenctl-almighty-0.2.0-linux-x86_64-preview.manifest.json) |
| ARM64 / aarch64 | [aarch64 tar.gz](ugreenctl-almighty-0.2.0-linux-aarch64-preview.tar.gz) | [manifest](ugreenctl-almighty-0.2.0-linux-aarch64-preview.manifest.json) |

下载压缩包时使用 GitHub 文件页面的 Download raw file；校验文件为 [SHA256SUMS](SHA256SUMS)。
将两份压缩包、两份 manifest 和校验文件下载到同一目录后运行：

```sh
sha256sum -c SHA256SUMS
tar -xzf ugreenctl-almighty-0.2.0-linux-x86_64-preview.tar.gz
cd ugreenctl-almighty-0.2.0-linux-x86_64-preview
./usr/local/bin/ugreenctl --plugin-dir "$PWD/usr/local/lib/ugreenctl/models" models
```

ARM64 选择对应压缩包及目录名。示例只解包并列出模型，不安装或访问硬件。
包内包含四个程序、九个模型插件、可选的 Python N76 手动构建入口，以及许可证、证据、
兼容目录、服务示例与完整测试日志。各文件 SHA-256 和模式均列在 manifest 中。

N76 入口位于 `usr/local/bin/ugreenctl-n76`，配套资源位于
`usr/local/share/ugreenctl/n76/`；需要 Python 3.10+，保留完整目录关系。它默认只预览，
显式 `build --apply` 才为匹配当前内核的 DH2600／DXP480T Plus 编译驱动。
没有附带预编译 N76 `.ko`、原厂驱动源码、NAS 固件或第三方运行库；不自动安装、加载或
绑定驱动。详细步骤与内核开发材料要求见包内的 `N76_MANUAL_BUILD.zh-CN.md`。

验证：两个架构 Release 配置、构建及完整 CTest 均为 29/29；原生运行库和插件验证见
包内记录。N76 的两款各三模块另在隔离环境下对 6.18.15 头文件实际编译通过，不能用于
推定其他内核或真实 NAS 加载成功。

**这是 `experimental-incomplete` 预览。** 当前25款范围仍有8款 M.2槽号、两款 N76
真实输出回读和 GT 的 BIOS/SATA 路由等缺口，尚无本项目25款 LED/盘位实机验收。
N76 手动构建组件不改变这些能力状态。当前工具依赖动态 glibc，数值符号下限为2.27；
目标系统仍需满足具体加载器、驱动和权限要求。部署以包内的 `LINUX_DEPLOYMENT.zh-CN.md`
及 `models/compatibility.json` 为准。TrueNAS 指 Linux/SCALE；当前不是 FreeBSD/CORE 包。
