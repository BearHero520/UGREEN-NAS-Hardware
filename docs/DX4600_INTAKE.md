# DX4600 family intake

## Identity

- Plugin ID: `dx4600`
- Marketing/product strings found in firmware: `DX4600`, `DX4600+`,
  `DX4600 Pro`
- Exact DMI product name(s): `DX4600`, `DX4600+`, `DX4600 Pro`, recovered
  from the official firmware product tables; capture from a tested device is
  still required
- Board revision: unknown
- CPU architecture: x86-64
- Firmware version and build date: UGOS Pro `1.17.0.0095`,
  `20260630.111337`

## Initial support level

- [ ] profile-only
- [ ] read-only
- [x] reverse-engineered plugin
- [ ] supported
- [ ] blocked

Reason: firmware routing, controller identity, registers, stock userspace fan
policy, LED route, beeper route, and SATA status route have been recovered.
The official image was confirmed by the supplier, so the three exact
firmware product strings now route to a compiled plugin. Fan, power-recovery,
WOL, and stock software-curve writes remain reverse-engineered and require
`--force --apply` until model-specific physical validation is supplied.

## Controller and transport

- Fan/power-recovery controller: ITE IT8613, revision read from config register
  `0x22` but the actual value is not present in the static image
- Fan/power-recovery transport: Super I/O config `0x2e/0x2f`, HWM
  `0xa35/0xa36`, LDN `0x04`
- LED controller: HT32F52231-family MCU through I2C/SMBus address `0x3a`
- Beeper: legacy PC speaker/PIT ports
- SATA presence: four firmware-hardcoded MMIO status addresses
- Stock modules/services: `ug_it86x-sio`, `leds-mcu-28a48`,
  `ug_sata_beep-dx4700`, `ug_gpio_btn`, `hwmonitor`
- Conflicting modules/services: vendor `/proc/it86`, `ug_it86x_sio`,
  `hwmonitor`, generic `it87`, and the bound LED MCU driver for direct I2C
- Required privileges: root plus raw I/O privileges for any future direct
  Super I/O access

## Validated capabilities

| Capability | Read tested | Write tested | Proposed safe range | Firmware tested | Notes |
| --- | --- | --- | --- | --- | --- |
| Fan | No | No | `40..255` guarded direct path | 1.17.0.0095 static only | One system fan, `0x17/0x73`, tach `0x1a/0x0f`; plugin implemented |
| LED | No | No | Not established | 1.17.0.0095 static only | Prefer LED class/sysfs; no direct MCU protocol claim |
| Power recovery | No | No | on/off/restore after physical validation | 1.17.0.0095 static only | `0xf2/0xf4` |
| WOL | No | No | Not established | 1.17.0.0095 static only | Stock route calls ethtool on `eth0` and `eth1` |
| Beeper | No | No | Not established | 1.17.0.0095 static only | PC speaker/PIT, vendor proc interface |
| SATA presence | No | No | Read-only candidate | 1.17.0.0095 static only | MMIO labels need slot-by-slot validation |

## Evidence

- Analysis date: 2026-07-28
- Input: `E:\工作台\固件\4600.img`
- Input SHA-256:
  `7bb2746324ac852475727cf23f7016517996b91dfb293d2292d531c1e71581b0`
- Test method: container inspection, verified layer reuse, ELF symbols,
  relocations, static data, strings, and x86-64 disassembly
- Physical expected/observed result: not tested
- Recovery/rollback procedure: no hardware transaction was issued
- Detailed record:
  [DX4600_REVERSE_ENGINEERING.zh-CN.md](DX4600_REVERSE_ENGINEERING.zh-CN.md)

## Catalog change

- [x] Added or updated `models/compatibility.json`.
- [ ] DMI strings are exact and captured from the tested device.
- [x] Every unsupported capability remains unverified or blocked.
- [x] Documentation describes conflicts and firmware limits.

The `dx4600` runtime plugin exposes only the firmware-reversed fan,
power-recovery, and WOL paths. LED, beeper, and SATA MMIO operations remain
unexposed.
