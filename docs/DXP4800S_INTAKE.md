# DXP4800S model intake / 新机型接入记录

## Identity / 身份信息

- Plugin ID: `dxp4800s`
- Marketing name: UGREEN DXP4800S
- Exact DMI product name(s): `DXP4800S`
- Board revision: `unknown`
- CPU architecture: x86_64
- Firmware version and build date: UGOS Pro `1.17.0.0095`,
  build `20260630.111337`, kernel `6.12.30+`

## Initial support level / 初始支持等级

- [ ] profile-only
- [ ] read-only
- [x] reverse-engineered
- [ ] supported
- [ ] blocked

Reason / 原因：原厂模块的控制器身份、访问时序、风扇和来电启动寄存器已经完成
静态逆向，但尚无精确 DXP4800S 实机读写记录。

## Controller and transport / 控制器与传输层

- Controller chip and revision: ITE IT8613 (`0x8613`); revision `unknown`
- Transport: Super I/O, not ACPI EC
- Configuration ports: `0x2e/0x2f`
- Hardware-monitor ports: `0xa35/0xa36`
- Stock kernel modules or services: `ug_it86x_sio`, `hwmonitor`
- Conflicting modules/services: `/proc/it86`, `ug_it86x_sio`,
  `ug_it86x_cpufan`, generic `it87`; stock `hwmonitor` must be stopped or
  replaced before manual control
- Required privileges: root or `CAP_SYS_RAWIO`

## Validated capabilities / 已验证能力

| Capability | Read tested | Write tested | Safe range | Firmware tested | Notes |
| --- | --- | --- | --- | --- | --- |
| Fan RPM | static firmware path only | n/a | n/a | 1.17.0.0095 | HWM `0x1a/0x0f`, `675000/tach`; physical result unknown |
| Fan PWM | current value unknown | static firmware path only | `64..255` in plugin | 1.17.0.0095 | HWM `0x17/0x73`; 64 is a static safety inference, not a physical stall limit |
| Fan stop | no | no | blocked | 1.17.0.0095 | Stock module has explicit `off`, but ordinary plugin writes do not expose zero |
| Automatic fan | no | no | unknown | 1.17.0.0095 | Stock behavior is a user-space temperature daemon, not hardware auto |
| LED | no | no | unknown | 1.17.0.0095 | Separate HT32F52231 MCU path; not exposed by this plugin |
| Power recovery | static firmware path only | static firmware path only | on/off/restore | 1.17.0.0095 | Super I/O `0xf2/0xf4`; physical result unknown |

## Evidence / 验证证据

- Test date: no physical test performed
- Firmware image SHA-256:
  `7bb2746324ac852475727cf23f7016517996b91dfb293d2292d531c1e71581b0`
- `ug_it86x-sio.ko` SHA-256:
  `4d3864794d9cdec926b666878aed4431f80b69108acceadfa21322245f1393f4`
- `hwmonitor` SHA-256:
  `e3a2891a20fcdbab1c90374eff0ba9b5d6840634021bb3d43e681831e89b03be`
- Test method: strings, ELF symbols, relocations, static data, and targeted
  x86-64 disassembly; cross-checked against the exact DMI branch, service, and
  `/etc/default/dxp4800.conf`
- Expected versus observed result: no physical observation; `unknown`
- Recovery/rollback procedure: stop the replacement controller, reload
  `ug_it86x_sio`, restart stock `hwmonitor`, or reboot into UGOS Pro
- Detailed evidence:
  [DXP4800S_REVERSE_ENGINEERING.md](DXP4800S_REVERSE_ENGINEERING.md)

## Catalog change / 目录变更

- [x] Added or updated `models/compatibility.json`.
- [x] DMI string is exact and captured from the stock firmware branch.
- [x] Every unsupported capability remains unverified or blocked.
- [x] Documentation describes conflicts and firmware limits.
- [ ] Physical read validation completed.
- [ ] Physical write validation completed.
