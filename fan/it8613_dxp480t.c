#include "fan/it8613_dxp480t.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "fan/it8613_direct.h"
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

static const struct it8613_direct_channel direct_status_channels[] = {
    {"cpu", 0x16, 0x6b, 0x0f, 0x1a},
    {"sys1", 0x17, 0x73, 0x0e, 0x19},
    {"sys2", 0x7f, 0x7b, 0x80, 0x81}
};

/* Preserve the vendor all-fans transaction order exactly. */
static const struct it8613_direct_channel direct_all_write_channels[] = {
    {"cpu", 0x16, 0x6b, 0x0f, 0x1a},
    {"sys2", 0x7f, 0x7b, 0x80, 0x81},
    {"sys1", 0x17, 0x73, 0x0e, 0x19}
};

int it8613_dxp480t_read_fans(struct ugreenctl_fan_status *fans, size_t *fan_count,
                              char *error, size_t error_size)
{
    int result = it8613_hwmon_read_fans(status_channels, 3, fans, fan_count,
                                        error, error_size);

    if (result != -ENODEV) {
        return result;
    }
    return it8613_direct_read_fans(direct_status_channels, 3, fans, fan_count,
                                   error, error_size);
}

int it8613_dxp480t_set_fan_pwm(bool force, const char *fan_id, uint8_t pwm,
                                char *error, size_t error_size)
{
    int result;

    if (strcmp(fan_id, "cpu") != 0 && strcmp(fan_id, "all") != 0) {
        (void)snprintf(error, error_size,
                       "unknown DXP480T fan target '%s' (expected cpu or all)", fan_id);
        return -EINVAL;
    }
    if (strcmp(fan_id, "cpu") == 0) {
        result = it8613_hwmon_set_manual_pwm(&status_channels[0], 1, pwm,
                                              error, error_size);
        if (result != -ENODEV) {
            return result;
        }
        if (!force) {
            (void)snprintf(error, error_size,
                           "direct Super I/O fan writes require --force together with --apply");
            return -EPERM;
        }
        return it8613_direct_set_manual_pwm(&direct_status_channels[0], 1, pwm,
                                            error, error_size);
    }
    result = it8613_hwmon_set_manual_pwm(all_write_channels, 3, pwm,
                                         error, error_size);
    if (result != -ENODEV) {
        return result;
    }
    if (!force) {
        (void)snprintf(error, error_size,
                       "direct Super I/O fan writes require --force together with --apply");
        return -EPERM;
    }
    return it8613_direct_set_manual_pwm(direct_all_write_channels, 3, pwm,
                                        error, error_size);
}
