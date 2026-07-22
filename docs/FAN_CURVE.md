# Software fan curves

`ugreenctl-fand` is a user-space temperature watchdog. It is the supported
replacement for the vendor `hwmonitor` process on systems that use this
project; it is **not** an IT8613 hardware automatic mode.

The daemon samples the highest temperature from Linux hwmon sources named
`coretemp`, `k10temp`, `zenpower`, `cpu_thermal`, or `acpitz` (CPU),
`drivetemp` (SATA HDD), and `nvme`/`nvme-pci` (NVMe). Every calculated PWM is
written back through the exact-DMI `ugreenctl` plugin. The existing controller
owner checks, process lock, minimum PWM floor, manual-mode selection and
readback therefore still apply to every update.

## Recovered vendor profiles

The following points were read from the vendor universal image supplied for
DXP480T Plus, DXP6800 Pro, DXP4800S and DXP4800 Plus. All four files have the
same SHA-256 (`7bb2746324ac852475727cf23f7016517996b91dfb293d2292d531c1e71581b0`).
Its `hwmonitor-480t` binary selects the corresponding `/etc/default/*.conf`
file by exact DMI product name; the generic `hwmonitor` selects DXP4800S.

`stop / start / mid / full / max` below are degrees Celsius. `sys` and `CPU`
are the vendor PWM points for those channels. The daemon preserves the points
and linearly interpolates between them; it never writes a stop/zero PWM.

| DMI product | Profile | CPU thresholds | HDD thresholds | NVMe thresholds | System PWM | CPU PWM |
| --- | --- | --- | --- | --- | --- | --- |
| DXP4800 | `stock-4800` | 45 / 50 / 70 / 75 / 85 | 35 / 40 / 45 / 50 / 65 | 40 / 45 / 55 / 60 / 65 | 64 / 128 / 204 / 255 | same channel |
| DXP4800S | `stock-4800s` | 50 / 55 / 75 / 80 / 90 | 40 / 45 / 50 / 55 / 70 | 45 / 50 / 60 / 65 / 70 | 64 / 128 / 204 / 255 | same channel |
| DXP4800 Plus / Pro | `stock-4800plus` | 42 / 50 / 70 / 78 / 90 | 30 / 40 / 46 / 52 / 55 | 50 / 55 / 60 / 65 / 70 | 65 / 125 / 200 / 235 | 60 / 125 / 205 / 230 |
| DXP480T Plus | `stock-480tplus` | 25 / 55 / 75 / 85 / 95 | disabled (0 / 0 / 0 / 0 / 0) | 40 / 50 / 60 / 70 / 80 | 55 / 90 / 110 / 128 | 70 / 130 / 150 / 200 |
| DXP6800 Pro | `stock-6800pro` | 25 / 38 / 55 / 75 / 90 | 30 / 35 / 43 / 48 / 55 | 45 / 50 / 60 / 65 / 70 | 64 / 130 / 210 / 230 | 80 / 130 / 210 / 230 |

DXP4800 Plus/Pro also has a vendor system-fan CPU floor of 65°C → PWM 100 and
90°C → PWM 205. DXP6800 Pro uses 65°C → PWM 125 and 90°C → PWM 220. The
system target is the highest active CPU/HDD/NVMe result and this CPU floor;
the CPU target follows its separate CPU-channel points.

DXP480T Plus exposes one vendor `all` transaction. Its implementation has one
physical duty output, so the daemon writes the higher of the recovered CPU and
system targets. This avoids two sequential channel writes racing on the shared
output while retaining the conservative result.

A stock profile is rejected unless it exactly matches the detected DMI model.
`custom` remains available for every supported model and retains the prior
single, hottest-source curve on both writable channels.

## Configuration

The daemon accepts a root-owned `key=value` configuration file. The app writes
the following values after validating them in both the adapter and daemon:

```ini
profile=custom                 # or a stock-* profile listed above
interval_seconds=10            # 2..300
downshift_delay_seconds=60     # 0..3600
minimum_pwm=64                 # 40..255, custom mode
failsafe_pwm=255
require_storage_sensor=false
allow_unvalidated_writes=false
cpu=50,55,75,80,90             # custom mode
hdd=40,45,50,55,70             # custom mode
ssd=45,50,60,65,70             # custom mode
pwm=64,128,204,255             # custom mode
```

Stock profiles intentionally replace curve input from the configuration with
the recovered points above. The timing, storage-presence safety switch,
failsafe PWM and protected-write acknowledgement remain configurable.

CPU temperature is mandatory; if it is unavailable, or storage is required
but both storage sources are unavailable, the daemon writes the configured
failsafe PWM (255 by default). A lower PWM must remain the desired value for
`downshift_delay_seconds` before it is written, preventing rapid fan hunting.

Typical direct use is:

```sh
sudo ugreenctl-fand --config /etc/ugreenctl/fan-curve.conf \
  --state /run/ugreenctl/fan-curve.state \
  --ugreenctl /usr/bin/ugreenctl \
  --plugin-dir /usr/lib/ugreenctl/models
```

The LLLED_FPK adapter supplies these paths and exposes status; it never writes
fan sysfs or Super I/O nodes directly.
