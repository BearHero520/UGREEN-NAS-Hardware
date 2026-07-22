#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fan/it8613_direct.h"
#include "fan/it8613_dxp4800.h"
#include "fan/it8613_hwmon.h"

static const struct it8613_direct_channel *captured_channels;
static size_t captured_channel_count;
static uint8_t captured_pwm;

static void fail(const char *message)
{
    (void)fprintf(stderr, "FAIL: %s\n", message);
    exit(EXIT_FAILURE);
}

int it8613_hwmon_read_fans(const struct it8613_hwmon_channel *channels,
                           size_t channel_count,
                           struct ugreenctl_fan_status *fans,
                           size_t *fan_count,
                           char *error, size_t error_size)
{
    (void)channels;
    (void)channel_count;
    (void)fans;
    (void)fan_count;
    (void)error;
    (void)error_size;
    return -ENODEV;
}

int it8613_hwmon_set_manual_pwm(const struct it8613_hwmon_channel *channels,
                                size_t channel_count, uint8_t pwm,
                                char *error, size_t error_size)
{
    (void)channels;
    (void)channel_count;
    (void)pwm;
    (void)error;
    (void)error_size;
    return -ENODEV;
}

int it8613_direct_read_fans(const struct it8613_direct_channel *channels,
                            size_t channel_count,
                            struct ugreenctl_fan_status *fans,
                            size_t *fan_count,
                            char *error, size_t error_size)
{
    (void)fans;
    (void)error;
    (void)error_size;
    captured_channels = channels;
    captured_channel_count = channel_count;
    *fan_count = channel_count;
    return 0;
}

int it8613_direct_set_manual_pwm(const struct it8613_direct_channel *channels,
                                  size_t channel_count, uint8_t pwm,
                                  char *error, size_t error_size)
{
    (void)error;
    (void)error_size;
    captured_channels = channels;
    captured_channel_count = channel_count;
    captured_pwm = pwm;
    return 0;
}

static void expect_map(void)
{
    const struct it8613_direct_channel *channel = &captured_channels[0];

    if (captured_channel_count != 1 || strcmp(channel->id, "sys") != 0 ||
        channel->control_register != 0x17 || channel->duty_register != 0x73 ||
        channel->tachometer_low_register != 0x0f ||
        channel->tachometer_high_register != 0x1a || !channel->pwm_supported) {
        fail("DXP4800 direct channel map");
    }
}

int main(void)
{
    char error[256] = {0};
    struct ugreenctl_fan_status fans[UGREENCTL_MAX_FANS];
    size_t fan_count = 0;

    if (it8613_dxp4800_read_fan(fans, &fan_count, error, sizeof(error)) != 0 ||
        fan_count != 1) {
        fail("DXP4800 direct status fallback");
    }
    expect_map();
    if (it8613_dxp4800_set_fan_pwm("sys", 120, error, sizeof(error)) != 0 ||
        captured_pwm != 120) {
        fail("DXP4800 direct PWM fallback");
    }
    expect_map();
    if (it8613_dxp4800_set_fan_pwm("cpu", 120, error, sizeof(error)) != -EINVAL ||
        strstr(error, "expected sys") == NULL) {
        fail("DXP4800 target guard");
    }

    (void)puts("DXP4800 direct map tests passed");
    return EXIT_SUCCESS;
}
