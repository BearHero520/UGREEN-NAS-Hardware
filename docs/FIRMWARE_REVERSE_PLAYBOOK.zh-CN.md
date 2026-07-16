# 系统包与固件逆向操作手册

这份手册记录的是可复用的**思考方法**，不是只适用于某一个固件的固定命令。不同厂商版本可能使用 tar、SquashFS、ext4、cpio、UPK/UGB、安装脚本或多层私有容器，实际操作必须根据文件证据调整。

## 一、先定义问题，不要先拆包

开始前先把需求改写成可验证的问题：

- 要控制什么：风扇、LED、蜂鸣器、来电启动、硬盘供电、显示屏还是传感器？
- 需要读取、写入，还是两者都需要？
- 目标系统是什么：原厂 Linux、其他 Linux、Windows、BSD，还是容器环境？
- 目标机型的精确 DMI product name 是什么？
- 用户是否有实机，能否进行只读验证或受控写入验证？

这样做可以避免把整个系统包无差别反编译。逆向工作的目标不是“看完所有文件”，而是建立从机型识别到硬件控制入口的证据链。

## 二、输入取证

原始包始终只读。第一轮记录：

```text
文件名
绝对路径
文件大小
SHA-256
文件 magic/真实格式
声明版本
Build 时间
架构
内核版本
```

不要只相信扩展名。例如 `.img` 可能实际是 tar，`.upk` 可能是带签名头的多层压缩包。

建议把镜像放到：

```text
firmware-inbox/input/
```

将全部临时输出放到：

```text
firmware-inbox/work/<包名或机型>/
```

输入文件不得原地解压、覆盖、改名或删除。

## 三、识别容器层级

每剥开一层都重新检查 magic，不假定下一层格式：

| 观察结果 | 下一步思路 |
| --- | --- |
| tar/ustar | 先列目录和校验文件，再选择性提取 |
| SquashFS | 先列文件树，再提取脚本、配置、模块和目标二进制 |
| cpio/initramfs | 解出启动脚本、modules、udev 和 early userspace 配置 |
| ext2/3/4 | 只读挂载或使用文件系统解析工具 |
| gzip/xz/zstd | 解压后再次识别，而不是直接认定最终内容 |
| ELF `.ko` | 转入内核模块分析流程 |
| ELF 用户程序 | 查字符串、动态依赖、符号、调用关系和配置路径 |
| 私有包头 | 解析“字段名:长度:数据”，按长度切分后再检查各字段 magic |
| 高熵且无已知 magic | 检查是否加密、签名、偏移封装或需要升级程序解包 |

先“列目录”，后“选择性提取”。只有确认确实需要全量文件时才完整解包。

## 四、建立机型到驱动的路由

最有价值的第一批文件通常不是反汇编，而是：

- `dmidecode` 或 product name 判断脚本；
- 模块加载脚本、systemd service、udev rules；
- `/etc/default/*.conf`、风扇曲线、LED 配置；
- 启动和关机脚本；
- `modprobe`、`insmod`、`rmmod` 调用；
- `/proc`、`/sys/class`、`/dev` 控制节点使用者。

目标是先画出最短链路：

```text
精确 DMI
  -> 原厂分支脚本
  -> 加载的模块/服务
  -> 暴露的 proc/sysfs/device 接口
  -> 实际控制器和寄存器
```

如果两个机型名字相近但加载不同模块，就必须按不同硬件方案处理。

## 五、判断控制器类型

不要看到“硬件控制”就默认是 EC。常见路径包括：

- ACPI Embedded Controller：ACPI EC opregion、`/sys/kernel/debug/ec`、EC command/data port。
- Super I/O：ITE/Nuvoton/Fintek 配置端口、logical device、Hardware Monitor index/data port。
- I2C/SMBus MCU：I2C 地址、chip ID、块读写、LED class 或 hwmon 驱动。
- GPIO：GPIO descriptor、ACPI GPIO、platform driver。
- PCI/PCIe MMIO：BAR、`ioremap`、设备 ID。
- ACPI/WMI：方法名、GUID、ACPI method。

判断依据必须来自模块字符串、芯片 ID 比较、端口 I/O、总线注册函数或原厂脚本，不能仅凭产品系列猜测。

## 六、内核模块分析顺序

对目标 `.ko` 建议按以下顺序进行，先便宜后昂贵：

1. 记录模块 SHA-256、vermagic、depends 和 modinfo。
2. 提取可打印字符串，搜索：
   - DMI 名称；
   - proc/sysfs 节点；
   - 风扇、PWM、RPM、LED、startup、beeper；
   - I2C、GPIO、Super I/O、EC、chip ID；
   - 错误日志和格式字符串。
3. 查看符号表，定位 `init`、`probe`、`read`、`write`、`set_*`、`remove`。
4. 查看重定位，把反汇编中的匿名地址还原成字符串、全局数组和内核 API。
5. 检查静态数据：DMI 表、I2C board info、颜色表、寄存器表、风扇通道数组。
6. 只反汇编关键函数，不必先反汇编整个模块。
7. 将写入路径和读取路径分别记录，确认范围检查、模式位、锁和错误处理。

需要回答的核心问题：

```text
芯片如何识别？
配置端口/总线地址是什么？
选择哪个 logical device？
索引/数据端口是什么？
读写哪些寄存器？
控制位和 PWM 值如何组合？
转速如何换算？
原厂驱动暴露哪些节点，权限是什么？
哪些模块或服务会并发访问同一硬件？
```

## 七、用户态程序和服务分析

内核模块说明“怎么写硬件”，用户态服务通常说明“什么时候写、写多少”：

- 查找温度阈值、PWM 曲线和自动/手动状态机。
- 查找 `/proc`、sysfs、ioctl、D-Bus、socket 和配置文件调用。
- 确认当前 PWM 是真实读取、不可读，还是仅缓存最后一次写入。
- 区分硬件自动模式与软件守护进程自动调速。
- 对 stripped Go 程序，可在必要时恢复 Go pclntab/函数名，再只分析目标包函数。

如果原厂自动调速由守护进程不断写 PWM，移植到其他系统时也需要一个软件控制循环，不能只提供一次性“自动”开关。

## 八、DXP4800 Plus 示例：结论是怎样得到的

这只是方法示例，不能当作其他机型的默认映射。

最终结论：DXP4800 Plus 不是 ACPI EC，而是 IT8613 Super I/O：

```text
配置端口          0x2e / 0x2f
Hardware Monitor 0xa35 / 0xa36
CPU PWM           0x16 / 0x6b
系统风扇 PWM      0x17 / 0x73
来电启动          0xf2 / 0xf4
```

推理过程应记录为：

1. 从原厂机型分支确认 DXP4800 Plus 加载的风扇/Super I/O 模块。
2. 在模块初始化函数中找到 `0x2e/0x2f` 进入配置模式和 IT8613 芯片 ID 校验。
3. 从 logical device 选择和端口寄存器恢复 Hardware Monitor 基址 `0xa35/0xa36`。
4. 分别跟踪 CPU、系统风扇的 read/write 函数，确认控制寄存器、PWM 寄存器和手动模式位。
5. 跟踪 startup read/write 函数，确认 `0xf2/0xf4` 的 on/off/last 组合。
6. 对照原厂脚本和配置，确认这些路径属于精确 DMI，而不是同系列泛用代码。
7. 在用户态实现前增加 DMI、芯片 ID、原厂驱动冲突、进程锁和最低 PWM 检查。
8. 构建并测试；实机验证与静态逆向分开记录。

这个过程的关键不是记住五组地址，而是学会从“机型路由 → 驱动初始化 → 关键读写函数 → 用户态策略”建立闭环。

## 九、证据表和停止条件

建议每个结论都写入表格：

| 结论 | 证据文件 | 函数/字符串/偏移 | 等级 | 实机状态 |
| --- | --- | --- | --- | --- |
| 控制器类型 |  |  | 固件证据/静态推断 |  |
| 精确 DMI |  |  | 固件证据 |  |
| 读寄存器 |  |  |  |  |
| 写寄存器 |  |  |  |  |
| 安全范围 |  |  |  |  |
| 冲突模块 |  |  |  |  |

遇到以下情况应停止写入实现并标记 `unknown`：

- 芯片 ID 或控制器类型不明确；
- 同一机型存在多个硬件 revision，但无法区分；
- 只找到写地址，没有确认模式位或值域；
- 原厂驱动和直接端口访问可能并发；
- 自动调速逻辑不完整；
- 需要实机观察才能区分多个通道。

## 十、接入 reference-hardware

证据足够后再实施：

1. 复制 `docs/NEW_MODEL_TEMPLATE.md`，记录精确 DMI、固件和证据等级。
2. 先建立 profile-only 机型插件。
3. 确认读取路径后开放 read-only。
4. 每次只增加一个经过证明的写能力。
5. 硬件通用代码放入对应硬件层，机型文件只做能力组合。
6. 更新 `models/compatibility.json` 和支持文档。
7. 运行构建和测试。

## 十一、下次提问模板

将镜像放进 `firmware-inbox/input/` 后，可以直接这样提问：

```text
请按 reference-hardware/docs/FIRMWARE_REVERSE_PLAYBOOK.zh-CN.md
逆向 firmware-inbox/input/<文件名>。

目标机型：<精确型号；不知道就先从固件找>
重点能力：<风扇/LED/来电启动/蜂鸣器/系统信息等>
目标系统：<Linux/Windows/其他>

要求：
1. 原包只读，先记录 SHA-256、真实格式、版本、Build 和内核。
2. 临时提取到 firmware-inbox/work/<名称>/。
3. 先建立 DMI -> 驱动/服务 -> 控制接口的证据链。
4. 区分固件证据、静态推断、实机验证和 unknown。
5. 不猜寄存器，不套用相似机型。
6. 先输出逆向结论和风险；证据足够时再接入 reference-hardware。
```

如果只想调查、不想改代码，在末尾增加：

```text
本次只做只读逆向和文档记录，不修改控制实现。
```
