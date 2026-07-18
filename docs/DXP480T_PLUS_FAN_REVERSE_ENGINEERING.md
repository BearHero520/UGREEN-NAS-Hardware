# DXP480T Plus direct-fan evidence

## Input and extraction

- Official input: `release_20260624-firmware_image-1.17.0.95-intel_release_amd64_6.12.30+ (1).img`
- Input SHA-256: `7bb2746324ac852475727cf23f7016517996b91dfb293d2292d531c1e71581b0`
- Outer format: GNU tar; relevant layers are `fw.squashfs` and `kernel.squashfs`
- Extracted module: `usr/lib/modules/6.12.30+/kernel/drivers/ugreen/dxp480t/ug_it86x-cpufan.ko`
- Module SHA-256: `677b740208097187caab302a16c9bf575478b46e455096f24c5ac284f2e8ccd7`
- Module identity: x86-64 relocatable ELF, not stripped, Build ID `132224fcf54363dbe2ee04c130a2f90f2d252859`
- Stock user-space controller: `rootfs-base.squashfs:usr/sbin/hwmonitor-480t`
- Controller SHA-256: `b847151fa0958bc035e7bb66a6664a4b4571f47977e0d1b3a487b31fd7903773`
- Controller identity: x86-64 PIE ELF with debug information, Build ID `335894cab8ba58db024ceea955851eb6ab226f8c`

The original input remains outside the repository. Temporary extraction is in
`firmware-inbox/work/dxp480t-plus-1.17.0.95-sys2/`.

## Model routing

`fw.squashfs:usr/sbin/ug-load-drive.sh` matches `DXP480T` and loads
`ug_it86x-cpufan`. The module contains the exact DMI string `DXP480T Plus` and
selects the IT8613 hardware-monitor logical device at `0xa35/0xa36`.

## Confirmed facts

- The module reads three tachometer pairs: `0x0f/0x1a`, `0x0e/0x19`, and
  `0x80/0x81`.
- In `set_fan.isra.0`, the exact `DXP480T Plus` DMI branch writes control
  register `0x17`, clears manual/automatic bit `0x80`, and writes duty
  register `0x73`.
- `hwmonitor-480t` calls `/proc/it86/fan` with `cpu <PWM>` and `set <PWM>`.
  In the same DXP480T Plus branch, `set_cpu_fan.isra.0` and
  `set_fan.isra.0` both select `0x17/0x73`.
- The code sequence using control `0x1e` and duty `0x7b` is in the separate
  DXP6800/FORT 6/DXP8800 branch. It is not evidence for DXP480T Plus.
- The stock DXP480T Plus branch contains no sysfan2 PWM write sequence.

## Runtime consequence

An earlier direct fallback guessed a sysfan2 control register and attempted an
`all` transaction in CPU, sysfan2, sysfan1 order. On a physical DXP480T Plus
with `it87` unloaded, CPU write/readback succeeded but the guessed sysfan2
manual-mode readback failed. This is insufficient evidence for another direct
write map.

The implementation therefore reproduces the stock shared `0x17/0x73` path for
both direct `cpu` and direct `all` writes, without requiring `it87`. It returns
sysfan2 RPM with unknown PWM/mode and does not attempt an independent sysfan2
write. A future sysfan2-specific path needs a DXP480T Plus-specific vendor write
sequence or controlled physical validation that distinguishes the channel and
mode bit.
