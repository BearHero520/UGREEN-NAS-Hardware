#include "fan/it8613_dxp480t.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "fan/it8613_hwmon.h"

static const struct it8613_hwmon_channel status_channels[] = {
    {"cpu", 2, 3},
    {"sys1", 3, 2},
    {"sys2", 4, 4}
};

/* Preserve the vendor all-fans transaction order exactly. */
static const struct it8613_hwmon_channel all_write_channels[] = {
    {"cpu", 2, 3},
    {"sys2", 4, 4},
    {"sys1", 3, 2}
};

int it8613_dxp480t_read_fans(struct ugreenctl_fan_status *fans, size_t *fan_count,
                              char *error, size_t error_size)
{
    return it8613_hwmon_read_fans(status_channels, 3, fans, fan_count,
                                  error, error_size);
}

int it8613_dxp480t_set_fan_pwm(const char *fan_id, uint8_t pwm,
                                char *error, size_t error_size)
{
    if (strcmp(fan_id, "cpu") != 0 && strcmp(fan_id, "all") != 0) {
        (void)snprintf(error, error_size,
                       "unknown DXP480T fan target '%s' (expected cpu or all)", fan_id);
        return -EINVAL;
    }
    if (strcmp(fan_id, "cpu") == 0) {
        return it8613_hwmon_set_manual_pwm(&status_channels[0], 1, pwm,
                                           error, error_size);
    }
    return it8613_hwmon_set_manual_pwm(all_write_channels, 3, pwm,
                                       error, error_size);
}
