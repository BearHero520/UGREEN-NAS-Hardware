# Firmware-derived Wake-on-LAN map

This record is the source of truth for the Wake-on-LAN (WOL) model mappings in
`ugreenctl`. It is static clean-room analysis of the official UGOS Pro
firmware, not a physical validation record.

## Inputs

The supplied official image files `4800.img`, `4800plus.img`,
`4800s/release_*.img`, and `480t/release_*.img` all contain the same universal
UGOS Pro image:

- SHA-256: `7bb2746324ac852475727cf23f7016517996b91dfb293d2292d531c1e71581b0`
- UGOS Pro `1.17.0.0095`, build `20260630.111337`, kernel `6.12.30+`
- WOL files extracted from `fw.squashfs`; the original images remain untouched.

The relevant stock startup selector is `etc/startpre.d/hwmonitor.sh`. Every
selected daemon below contains the explicit WOL commands
`ethtool -s eth0 wol g`, `ethtool -s eth1 wol g` and their `wol d`
counterparts, gated by `power.wake_on`.

| Exact runtime DMI product name | Stock daemon chosen by firmware | Daemon SHA-256 | Firmware WOL interface map |
| --- | --- | --- | --- |
| `DXP4800` | `hwmonitor-amd` | `4718169d5f2e462991e3480cc8100fba8c5bd38f6a6cbfdb6de65c7c7a3079c0` | `eth0`, `eth1` |
| `DXP4800S` | `hwmonitor` | `866c37a88a58917aaf7eb02a1f037847e63214d1c83d196253b92ad566147d6f` | `eth0`, `eth1` |
| `DXP4800 Plus` / `DXP4800 Pro` | `hwmonitor-480t` | `ab14a9602e208bb9e13f43b18a7ec6b8fb18cc6242f1ca3ab1a1b5fc9db1242a` | `eth0`, `eth1` |
| `DXP480T Plus` | `hwmonitor-480t` | `ab14a9602e208bb9e13f43b18a7ec6b8fb18cc6242f1ca3ab1a1b5fc9db1242a` | `eth0`, `eth1` |
| `DXP6800 Pro` | `hwmonitor-480t` through the `DXP6800` stock route | `ab14a9602e208bb9e13f43b18a7ec6b8fb18cc6242f1ca3ab1a1b5fc9db1242a` | `eth0`, `eth1` |

`ug-load-drive.sh` independently confirms the exact `DXP4800S`,
`DXP4800 Plus`, `DXP4800 Pro`, `DXP480T*`, and `DXP6800*` product routes.
The runtime plugins intentionally accept only their documented exact DMI names;
the firmware's prefix tests are not reproduced as fuzzy matching.

## Implementation and safeguards

WOL is NIC configuration, not a BIOS or Super-I/O setting. The implementation
uses the Linux ethtool ioctl equivalent to the stock command, never launches a
shell, and never touches the LED path. Each model keeps its own exact
`eth0`/`eth1` map in `network/wol_<model>.c`.

Before a write, `ugreenctl` checks magic-packet support on both mapped NICs.
It writes both NICs, then reads both back and rejects a mixed or mismatched
state. It deliberately does not guess renamed interfaces such as `enp*`.

All WOL writes require the normal `--apply` action gate and `--force`, because
WOL persistence through shutdown, reboot, and AC loss has not been physically
validated on these models. Reads remain available without a write override.

```sh
ugreenctl --model dxp480tplus network wol get
sudo ugreenctl --model dxp480tplus --force --apply network wol set on
```

Physical validation must confirm each NIC's WOL capability, a magic-packet
wake after shutdown, and persistence across reboot and AC removal before the
capability is promoted from `reverse-engineered`.
