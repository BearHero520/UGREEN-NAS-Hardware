#include "fan/it8613_dxp4800s.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "fan/it8613_hwmon.h"

static const struct it8613_hwmon_channel channel = {"sys", 3, 3};

int it8613_dxp4800s_read_fan(struct ugreenctl_fan_status *fans, size_t *fan_count,
                             char *error, size_t error_size)
{
    return it8613_hwmon_read_fans(&channel, 1, fans, fan_count,
                                  error, error_size);
}

int it8613_dxp4800s_set_fan_pwm(const char *fan_id, uint8_t pwm,
                                char *error, size_t error_size)
{
    if (strcmp(fan_id, "sys") != 0) {
        (void)snprintf(error, error_size,
                       "unknown DXP4800S fan target '%s' (expected sys)", fan_id);
        return -EINVAL;
    }
    return it8613_hwmon_set_manual_pwm(&channel, 1, pwm, error, error_size);
}
