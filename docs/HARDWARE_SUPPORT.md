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

- Preferred controller: Linux `it87` driver, hwmon node selected by `name=it8613`
- Fan channels: CPU and system
- AC recovery policy: on, off, restore previous state

Fan reads and writes prefer the existing hwmon node and the tool never unloads
`it87`. If that node is absent, a direct Super I/O fallback uses the exact
CPU/system mapping. It opens only after rejecting the vendor interfaces and an
active `it87` module, acquires the same process lock, confirms IT8613 identity,
and verifies every write. The fallback needs `--force --apply` because its direct
fan transaction is not yet separately physically validated. AC recovery remains
an independent Super I/O operation.

## DXP4800S

The `dxp4800s` plugin matches only the exact DMI product name `DXP4800S`.
Its map was recovered from UGOS Pro 1.17.0.0095 `ug_it86x-sio.ko` and the
stock `hwmonitor` model branch.

- Preferred controller: Linux `it87` driver, dynamic hwmon node `name=it8613`
- Single fan target: `sys` (stock name `sysfan1`)
- Exposed PWM range: `40..255`
- AC recovery registers: `0xf2/0xf4`
- Current PWM is read directly; only the supported manual mode is reported as
  known (the stock temperature policy is a user-space daemon)

Writes require exact DMI matching and `--force --apply`. They use the dynamic
`name=it8613` node when present; otherwise they use the recovered direct map
only after the vendor `/proc/it86` interface and generic `it87` module are both
inactive. Neither `--force` nor model selection bypasses ownership, chip-ID, or
lock checks. The direct path is firmware-reversed and awaits physical write
validation. See [DXP4800S_REVERSE_ENGINEERING.md](DXP4800S_REVERSE_ENGINEERING.md).

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

The firmware-recovered hwmon CPU/all-fans PWM and AC recovery paths have been
validated on a physical DXP480T Plus. Fan writes through hwmon require
`--apply`; each `pwm*_enable=1` and PWM write is read back while holding a
process lock. If the `it8613` hwmon node is absent, the same model-specific
CPU direct map is available only with `--force --apply`, no active vendor or
`it87` owner, an IT8613 identity match, and verified readback. The stock
DXP480T Plus branch uses one shared PWM output (`0x17/0x73`) for both its
`cpu` and `set` commands, so the direct `all` fallback reproduces that one
vendor control path rather than guessing three independent registers. Sysfan2
RPM is reported but its PWM and mode are unknown because the stock branch has
no sysfan2-specific write sequence. Only manual PWM is exposed: `pwm*_enable=2`
is not reported as the vendor software temperature curve. Independent system-fan
writes are not exposed. See
[DXP480T_PLUS_FAN_REVERSE_ENGINEERING.md](DXP480T_PLUS_FAN_REVERSE_ENGINEERING.md).

## Adding a model

1. Record the exact DMI product string and controller identity.
2. Add a profile-only model plugin first.
3. Add read-only status only after comparing it with stock firmware.
4. Add one write function at a time, with a safe default and documented map.
5. Include a hardware test record in the pull request.
