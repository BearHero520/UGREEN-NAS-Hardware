# Third-party notices

The legacy LED wire protocol and command-line compatibility behavior are based on
[miskcoo/ugreen_leds_controller](https://github.com/miskcoo/ugreen_leds_controller),
MIT license, copyright (c) 2024 miskcoo@gmail.com. The full license is preserved in
[led/LICENSE.upstream-led](led/LICENSE.upstream-led) and installed with the documentation.
The implementation is C and does not run or bundle the old CLI binary or kernel module.

Reference source commit: `f0fcd8192abdd92244aba9f6f0d10572f5561380`.
The upstream physical slot reports used by the new sysfs implementation, their
limitations, calibration instructions and attribution are in
[disk/NOTICE.md](disk/NOTICE.md).

Linux I2C userspace API documentation:
<https://www.kernel.org/doc/html/latest/i2c/dev-interface.html>.
No Linux kernel or systemd implementation source is copied into this project.

The optional manual N76 build component downloads selected UGREEN kernel LED
driver sources only when explicitly requested. The pinned upstream commit,
per-file SHA-256 values and URLs are in `tools/n76/sources.json`; upstream files
and generated kernel modules are not included in this repository or tool archive.
Generated driver/trigger files retain their upstream GPL identifiers and notices.
The separately identified local safety overlay
`tools/n76/ugreenctl_n76_identity.h` is GPL-2.0-or-later. Its read-only mock test
uses the same identifier. The Python preparation/build tools are project code.
See `docs/N76_MANUAL_BUILD.zh-CN.md` for provenance, build limits and the distinction
between driver caches and genuine hardware readback.
