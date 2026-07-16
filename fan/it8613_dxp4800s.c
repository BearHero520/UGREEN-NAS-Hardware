#include "fan/it8613_dxp4800s.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "superio/it86x.h"

#define DXP4800S_MIN_MANUAL_PWM 64U

static unsigned long tach_to_rpm(uint16_t tachometer)
{
    if (tachometer == 0 || tachometer == 0x0fff || tachometer == 0xffff) {
        return 0;
    }
    return 675000UL / tachometer;
}

static uint16_t read_tachometer(void)
{
    return ((uint16_t)it86x_hwm_read(0x1a) << 8) | it86x_hwm_read(0x0f);
}

int it8613_dxp4800s_read_fan(struct ugreenctl_fan_status *fans, size_t *fan_count,
                             char *error, size_t error_size)
{
    struct it86x_device device;
    uint16_t tachometer;
    int result = it86x_open(&device, error, error_size);

    if (result != 0) {
        return result;
    }
    tachometer = read_tachometer();
    (void)snprintf(fans[0].id, sizeof(fans[0].id), "sys");
    fans[0].pwm = 0;
    fans[0].pwm_known = false;
    fans[0].manual = false;
    fans[0].mode_known = false;
    fans[0].tachometer = tachometer;
    fans[0].rpm = tach_to_rpm(tachometer);
    *fan_count = 1;
    it86x_close(&device);
    return 0;
}

int it8613_dxp4800s_set_fan_pwm(const char *fan_id, uint8_t pwm,
                                char *error, size_t error_size)
{
    struct it86x_device device;
    uint8_t control;
    int result;

    if (strcmp(fan_id, "sys") != 0) {
        (void)snprintf(error, error_size,
                       "unknown DXP4800S fan target '%s' (expected sys)", fan_id);
        return -EINVAL;
    }
    if (pwm < DXP4800S_MIN_MANUAL_PWM) {
        (void)snprintf(error, error_size,
                       "refusing unvalidated PWM %u; DXP4800S manual control is limited to 64-255",
                       (unsigned int)pwm);
        return -EPERM;
    }
    result = it86x_open(&device, error, error_size);
    if (result != 0) {
        return result;
    }
    control = it86x_hwm_read(0x17);
    it86x_hwm_write(0x17, control & 0x7f);
    it86x_hwm_write(0x73, pwm);
    it86x_close(&device);
    return 0;
}
