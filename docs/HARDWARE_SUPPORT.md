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

- Controller: Linux `it87` driver, hwmon node selected by `name=it8613`
- Fan channels: CPU and system
- AC recovery policy: on, off, restore previous state

Fan reads and writes use the existing hwmon node; the tool never unloads `it87`
or performs a second direct Super I/O fan transaction. AC recovery remains an
independent Super I/O operation.

## DXP4800S

The `dxp4800s` plugin matches only the exact DMI product name `DXP4800S`.
Its map was recovered from UGOS Pro 1.17.0.0095 `ug_it86x-sio.ko` and the
stock `hwmonitor` model branch.

- Controller: Linux `it87` driver, dynamic hwmon node `name=it8613`
- Single fan target: `sys` (stock name `sysfan1`)
- Exposed PWM range: `40..255`
- AC recovery registers: `0xf2/0xf4`
- Current PWM and hardware/daemon mode: unknown

Writes require exact DMI matching, `--force --apply`, a dynamic `name=it8613`
node, and an inactive vendor `/proc/it86` driver. The generic `it87` module is
left in place. Neither `--force` nor model selection bypasses these checks. See
[DXP4800S_REVERSE_ENGINEERING.md](DXP4800S_REVERSE_ENGINEERING.md).

## DXP480T Plus

The dxp480tplus plugin matches the exact DMI product name DXP480T Plus. Its
ITE IT8613 map was recovered from the stock 1.17.0.95
ug_it86x-cpufan module.

- Input image SHA-256:
  7bb2746324ac852475727cf23f7016517996b91dfb293d2292d531c1e71581b0
- Fan status uses hwmon channels `pwm2/fan3_input` (CPU),
  `pwm3/fan2_input` (system fan 1), and `pwm4/fan4_input` (system fan 2).
- All-fans manual PWM writes are ordered CPU, system fan 2, then system fan 1.
- AC recovery policy: Super I/O registers 0xf2 and 0xf4
- LED hardware: N76E003 MCU via an I2C/SMBus driver; no ugreenctl LED write
  operation is exposed

The firmware-recovered CPU/all-fans PWM and AC recovery paths have been
validated on a physical DXP480T Plus. Fan writes require `--apply`; each
`pwm*_enable=1` and PWM write is read back while holding a process lock. Only
manual PWM is exposed: `pwm*_enable=2` is not reported as the vendor software
temperature curve. Exact DMI matching, the active vendor-driver guard, dynamic
hwmon discovery, and the minimum PWM guard remain mandatory. Independent
system-fan writes are not exposed.

## Adding a model

1. Record the exact DMI product string and controller identity.
2. Add a profile-only model plugin first.
3. Add read-only status only after comparing it with stock firmware.
4. Add one write function at a time, with a safe default and documented map.
5. Include a hardware test record in the pull request.
