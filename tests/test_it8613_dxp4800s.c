#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fan/it8613_direct.h"
#include "fan/it8613_dxp4800s.h"
#include "fan/it8613_hwmon.h"

static const struct it8613_direct_channel *captured_channels;
static size_t captured_channel_count;
static uint8_t captured_pwm;
static int hwmon_result = -ENODEV;
static unsigned direct_calls;

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
    if (channel_count != 1 || strcmp(channels[0].id, "sys") != 0 ||
        channels[0].pwm_index != 3 || channels[0].fan_index != 3)
        fail("DXP4800S hwmon mapping");
    (void)fans;
    (void)fan_count;
    (void)error;
    (void)error_size;
    return hwmon_result;
}

int it8613_hwmon_set_manual_pwm(const struct it8613_hwmon_channel *channels,
                                size_t channel_count, uint8_t pwm,
                                char *error, size_t error_size)
{
    if (channel_count != 1 || strcmp(channels[0].id, "sys") != 0 ||
        channels[0].pwm_index != 3 || channels[0].fan_index != 3)
        fail("DXP4800S hwmon mapping");
    (void)pwm;
    (void)error;
    (void)error_size;
    return hwmon_result;
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
    ++direct_calls;
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
    ++direct_calls;
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
        fail("DXP4800S direct channel map");
    }
}

int main(void)
{
    char error[256] = {0};
    struct ugreenctl_fan_status fans[UGREENCTL_MAX_FANS];
    size_t fan_count = 0;

    if (it8613_dxp4800s_read_fan(fans, &fan_count, error, sizeof(error)) != 0 ||
        fan_count != 1) {
        fail("DXP4800S direct status fallback");
    }
    expect_map();
    if (it8613_dxp4800s_set_fan_pwm("sys", 120, error, sizeof(error)) != 0 ||
        captured_pwm != 120) {
        fail("DXP4800S direct PWM fallback");
    }
    expect_map();
    if (it8613_dxp4800s_set_fan_pwm("cpu", 120, error, sizeof(error)) != -EINVAL ||
        strstr(error, "expected sys") == NULL) {
        fail("DXP4800S target guard");
    }

    const int results[] = {0, -EIO, -EBUSY, -EACCES};
    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); ++i) {
        hwmon_result = results[i];
        unsigned before = direct_calls;
        if (it8613_dxp4800s_read_fan(fans, &fan_count, error, sizeof(error)) != hwmon_result ||
            it8613_dxp4800s_set_fan_pwm("sys", 120, error, sizeof(error)) != hwmon_result ||
            direct_calls != before)
            fail("hwmon success/errors must not enter direct fallback");
    }
    (void)puts("DXP4800S mapping and fallback boundary tests passed");
    return EXIT_SUCCESS;
}
