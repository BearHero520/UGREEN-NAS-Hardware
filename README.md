# ugreenctl

ugreenctl is a clean-room, plugin-based hardware-management utility for UGREEN
NAS systems.

[中文文档](README.zh-CN.md) · [Compatibility catalog](docs/COMPATIBILITY.md) ·
[中文兼容性目录](docs/COMPATIBILITY.zh-CN.md)

Only a model with a verified hardware map receives write support. A profile in
the models/ directory is not a claim that its controls work.

## Support matrix

| Model plugin | State | Verified functions |
| --- | --- | --- |
| dxp4800plus | supported | CPU/system fan status and PWM, AC recovery policy |
| dxp2800, dxp4800, dxp6800pro, dxp8800plus | profile only | none |

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
    ugreenctl power startup get
    sudo ugreenctl --apply power startup set restore

Writes are previews until --apply is supplied. Use --model ID when DMI is
unavailable. --force bypasses DMI matching, the active vendor-driver guard,
the IT8613 identity check, and the safe minimum PWM guard; it is for hardware
investigation only.

## Safety

- The DXP4800 Plus plugin refuses to touch /proc/it86 while the vendor driver
  is active, avoiding concurrent register access.
- Each controller access takes an advisory lock at
  /run/ugreenctl-it8613.lock.
- Direct I/O requires root or CAP_SYS_RAWIO.
- Manual PWM disables automatic control for that fan. Do not use a low PWM or
  --force unless temperatures are independently monitored.

Before use outside UGREEN NAS firmware, unload the vendor module if it is
active:

    sudo modprobe -r ug_it86x_cpufan

## Plugin ABI

Each models/*.so exports the entrypoint:

    const struct ugreenctl_plugin *ugreenctl_plugin_v1(void);

The ABI is declared in include/ugreenctl.h. The core accepts ABI version 1
only and discovers plugins by DMI product name or --model.

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
