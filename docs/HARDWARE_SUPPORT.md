# Hardware support policy

ugreenctl distinguishes a model profile from supported hardware.

| Level | Meaning |
| --- | --- |
| Supported | Controller identity, access sequence, and every exposed write register are verified on the named model. |
| Read-only | The data source is verified, but no write operation is exposed. |
| Reverse-engineered | The stock command path and registers are recovered, but physical validation is pending; writes require model-specific safeguards plus `--force --apply`. |
| Profile only | The plugin participates in model discovery but exposes no hardware functions. |

Supported and explicitly guarded reverse-engineered plugins may provide write
commands. A placeholder must never map plausible-looking registers or reuse a
controller map from another model.

## DXP4800 Plus / DXP4800 Pro

The dxp4800plus plugin supports DMI product names DXP4800 Plus and DXP4800
Pro.

- Controller: ITE IT8613 Super I/O
- Super I/O config ports: 0x2e / 0x2f
- Hardware-monitor ports: 0xa35 / 0xa36
- Fan channels: CPU and system
- AC recovery policy: on, off, restore previous state

This map was observed from UGREEN firmware 1.17.0.95. The tool always verifies
the IT8613 identifier before access.

## DXP4800S

The `dxp4800s` plugin matches only the exact DMI product name `DXP4800S`.
Its map was recovered from UGOS Pro 1.17.0.0095 `ug_it86x-sio.ko` and the
stock `hwmonitor` model branch.

- Controller: ITE IT8613 Super I/O
- Single fan target: `sys` (stock name `sysfan1`)
- RPM registers: `0x1a/0x0f`
- Manual/PWM registers: `0x17/0x73`
- Exposed PWM range: `64..255`
- AC recovery registers: `0xf2/0xf4`
- Current PWM and hardware/daemon mode: unknown

Writes require exact DMI matching, `--force --apply`, a matching IT8613 ID,
and an inactive vendor `/proc/it86` driver. Neither `--force` nor model
selection bypasses the chip-identity or driver-conflict checks. This support
is firmware-reversed, not physically validated. See
[DXP4800S_REVERSE_ENGINEERING.md](DXP4800S_REVERSE_ENGINEERING.md).

## DXP480T Plus

The dxp480tplus plugin matches the exact DMI product name DXP480T Plus. Its
ITE IT8613 map was recovered from the stock 1.17.0.95
ug_it86x-cpufan module.

- Input image SHA-256:
  7bb2746324ac852475727cf23f7016517996b91dfb293d2292d531c1e71581b0
- Fan RPM registers: CPU 0x1a/0x0f, system fan 1 0x19/0x0e, system fan 2
  0x81/0x80
- Fan mode/PWM registers: CPU 0x16/0x6b, system fan 1 0x17/0x73, system fan 2
  0x1e/0x7b
- Vendor all-fans command: 0x16/0x6b, 0x1e/0x7b, then 0x17/0x73
- AC recovery policy: Super I/O registers 0xf2 and 0xf4
- LED hardware: N76E003 MCU via an I2C/SMBus driver; no ugreenctl LED write
  operation is exposed

The firmware-recovered CPU/all-fans PWM and AC recovery paths have been
validated on a physical DXP480T Plus. Writes require --apply. Exact DMI
matching, the active vendor-driver guard, the IT8613 identity check, and the
minimum PWM guard remain mandatory. Independent system-fan writes are not
exposed.

## Adding a model

1. Record the exact DMI product string and controller identity.
2. Add a profile-only model plugin first.
3. Add read-only status only after comparing it with stock firmware.
4. Add one write function at a time, with a safe default and documented map.
5. Include a hardware test record in the pull request.
