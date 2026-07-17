#include "fan/it8613_fan.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "fan/it8613_hwmon.h"

/* Linux it87 maps control/duty registers 0x16/0x6b to pwm2 and
 * 0x17/0x73 to pwm3. The tachometer wiring is model-specific. */
static const struct it8613_hwmon_channel channels[] = {
    {"cpu", 2, 2},
    {"sys", 3, 3}
};

int it8613_read_fans(bool force, struct ugreenctl_fan_status *fans, size_t *fan_count,
                     char *error, size_t error_size)
{
    (void)force;
    return it8613_hwmon_read_fans(channels, 2, fans, fan_count,
                                  error, error_size);
}

int it8613_set_fan_pwm(bool force, const char *fan_id, uint8_t pwm,
                       char *error, size_t error_size)
{
    const struct it8613_hwmon_channel *channel;

    if (strcmp(fan_id, "cpu") == 0) {
        channel = &channels[0];
    } else if (strcmp(fan_id, "sys") == 0) {
        channel = &channels[1];
    } else {
        (void)snprintf(error, error_size, "unknown fan '%s' (expected cpu or sys)", fan_id);
        return -EINVAL;
    }
    (void)force;
    return it8613_hwmon_set_manual_pwm(channel, 1, pwm, error, error_size);
}
