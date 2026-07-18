# ugreenctl

ugreenctl is a clean-room, plugin-based hardware-management utility for UGREEN
NAS systems.

[中文文档](README.zh-CN.md) · [Compatibility catalog](docs/COMPATIBILITY.md) ·
[中文兼容性目录](docs/COMPATIBILITY.zh-CN.md)

Verified models receive normal write support. A firmware-reversed model can
expose only guarded writes with explicit acknowledgement while physical
validation is pending. A profile in the models/ directory is not a claim that
its controls work.

## Support matrix

| Model plugin | State | Verified functions |
| --- | --- | --- |
| dxp4800plus | supported | CPU/system fan status and PWM, AC recovery policy; guarded direct fan fallback |
| dxp4800s | firmware-reversed | sysfan1 RPM; guarded sys fan PWM 40-255 and AC recovery require --force --apply |
| dxp480tplus | supported | CPU, sysfan1, sysfan2 RPM; hwmon CPU/all PWM and AC recovery; guarded shared-PWM direct fallback |
| dxp6800pro | firmware-reversed | CPU/sys1/sys2 status; guarded CPU and paired-system PWM, AC recovery require --force --apply |
| dxp2800, dxp4800, dxp8800plus | profile only | none |

The dxp4800plus plugin also matches the DMI product name DXP4800 Pro. It
targets the ITE IT8613 Super I/O hardware monitor observed in firmware
1.17.0.95.

## Build

Build on an x86/x86_64 Linux host:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure
    sudo cmake --install build

For a build-tree run, point the CLI at the generated plugins:

    ./build/ugreenctl --plugin-dir ./build/models models
    sudo ./build/ugreenctl --plugin-dir ./build/models info

## One-command scripts

Copy or clone the repository onto the Linux NAS, then run:

    sh ./scripts/install.sh --install-deps

This optionally installs the build prerequisites using the NAS package manager,
builds the project, and installs ugreenctl to /usr/local. Omit
--install-deps when CMake and a C compiler are already installed.

For a build-tree run without installing files:

    sh ./scripts/run.sh models
    sudo sh ./scripts/run.sh info
    sh ./scripts/run.sh fan set cpu 120
    sudo sh ./scripts/run.sh --apply fan set cpu 120

The last command is the only one that writes the fan register. The script
does not add --apply itself.

## Commands

    ugreenctl models
    ugreenctl info
    ugreenctl fan status
    ugreenctl fan set cpu 120
    sudo ugreenctl --apply fan set cpu 120
    sudo ugreenctl --apply fan mode cpu auto
    ugreenctl power startup get
    sudo ugreenctl --apply power startup set restore

Writes are previews until --apply is supplied. `--force` acknowledges a
firmware-reversed write path; it does not bypass exact DMI matching, the active
vendor-driver guard, or the IT8613 identity check.

DXP4800S has one recovered `sysfan1` channel. The plugin calls it `sys`, and
exposes a guarded manual range of 40-255:

    sudo ugreenctl --force --apply fan set sys 120
    sudo ugreenctl --force --apply power startup set restore

DXP6800 Pro is firmware-reversed and has not yet had a physical write
validation. It exposes the stock CPU path and the stock paired-system-fan path;
both still require `--force --apply`:

    sudo ugreenctl --force --apply fan set cpu 120
    sudo ugreenctl --force --apply fan set sys 120
    sudo ugreenctl --force --apply power startup set restore

`sys` sets both firmware-proven system PWM outputs with the same value. The
stock `hwmonitor` thermal curve is a user-space service, so it is not exposed
as a hardware automatic mode. The recovered register map and validation limits
are in [docs/DXP6800_PRO_REVERSE_ENGINEERING.md](docs/DXP6800_PRO_REVERSE_ENGINEERING.md).

The stock automatic mode is a user-space `hwmonitor` temperature daemon, not
an IT8613 hardware-auto switch. This utility currently provides the guarded
manual primitive; an alternate operating system needs its own temperature
watchdog before automatic control is claimed.

For DXP480T Plus, the firmware-recovered hwmon write paths have been validated
on a physical device. Normal hwmon writes require --apply and retain exact DMI matching,
the vendor-driver conflict guard, the IT8613 identity check, and the PWM floor:

    sudo ugreenctl --apply fan set cpu 120
    sudo ugreenctl --apply fan set all 120

With hwmon present, the all target follows the vendor's three-channel transaction
in vendor order. When `it87` was intentionally unloaded and the `it8613` hwmon
node is absent, DXP480T Plus uses the stock shared direct PWM output for both
`cpu` and `all`: control `0x17`, duty `0x73`. Its three tachometers remain
visible, but sysfan2 has no firmware-proven independent PWM path and is reported
with unknown PWM/mode. Independent system-fan writes remain hidden. As for the
other supported direct fallbacks, it refuses to run while any vendor interface or
`it87` owns the controller, checks chip identity and the process lock, and
requires `--force --apply`; physical validation of this shared direct all-fans
path is still pending.

## Safety

- Every IT8613 plugin refuses to touch the controller while `/proc/it86`
  is active, avoiding concurrent register access.
- Each controller access takes an advisory lock at
  /run/ugreenctl-it8613.lock.
- Direct I/O requires root or CAP_SYS_RAWIO.
- Manual PWM disables hardware automatic control for that channel. DXP4800S
  stock automatic control is software-based, so stop or replace the stock
  daemon before manual use and keep temperatures independently monitored.

Before use outside UGREEN NAS firmware, unload the vendor module if it is
active:

    sudo modprobe -r ug_it86x_sio       # DXP4800S / DXP4800 branch
    sudo modprobe -r ug_it86x_cpufan    # DXP4800 Plus / DXP480T / DXP6800 branch

## Plugin ABI

Models may export the ABI version 3 entrypoint for fan-mode control and keep
the ABI version 2 entrypoint for compatibility:

    const struct ugreenctl_plugin *ugreenctl_plugin_v3(void);

    const struct ugreenctl_plugin *ugreenctl_plugin_v2(void);

The ABI is declared in include/ugreenctl.h. The core prefers ABI version 3,
falls back to ABI version 2, and discovers plugins by DMI product name or
--model.

## Maintaining compatibility

The machine-readable [compatibility catalog](models/compatibility.json) is the
release-facing record for every model. Before adding a plugin, follow the
[new-model intake template](docs/NEW_MODEL_TEMPLATE.md). In particular, use
exact DMI names, declare a support level, and record firmware evidence. Do not
promote a profile-only plugin to read-only or supported based on a similar
model.

## Provenance

The DXP4800 Plus map is a clean-room reimplementation based on observable
behavior of UGREEN firmware 1.17.0.95 and its ug_it86x-cpufan.ko interface.
No vendor driver source or binary is linked or redistributed by this project.

The DXP4800S map is documented in
[docs/DXP4800S_REVERSE_ENGINEERING.md](docs/DXP4800S_REVERSE_ENGINEERING.md)
and remains firmware-reversed until a physical validation record is added.
