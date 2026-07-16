# Hardware support policy

ugreenctl distinguishes a model profile from supported hardware.

| Level | Meaning |
| --- | --- |
| Supported | Controller identity, access sequence, and every exposed write register are verified on the named model. |
| Read-only | The data source is verified, but no write operation is exposed. |
| Profile only | The plugin participates in model discovery but exposes no hardware functions. |

Only the supported level permits a write command. A placeholder must never map
plausible-looking registers or reuse a controller map from another model.

## DXP4800 Plus / DXP4800 Pro

The dxp4800plus plugin supports DMI product names DXP4800 Plus and DXP4800
Pro.

- Controller: ITE IT8613 Super I/O
- Super I/O config ports: 0x2e / 0x2f
- Hardware-monitor ports: 0xa35 / 0xa36
- Fan channels: CPU and system
- AC recovery policy: on, off, restore previous state

This map was observed from UGREEN firmware 1.17.0.95. The tool verifies the
IT8613 identifier before access unless the caller supplies --force.

## Adding a model

1. Record the exact DMI product string and controller identity.
2. Add a profile-only model plugin first.
3. Add read-only status only after comparing it with stock firmware.
4. Add one write function at a time, with a safe default and documented map.
5. Include a hardware test record in the pull request.
