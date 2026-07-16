#include "fan/it8613_fan.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "superio/it86x.h"

static unsigned long tach_to_rpm(uint16_t tachometer)
{
    if (tachometer == 0 || tachometer == 0x0fff || tachometer == 0xffff) {
        return 0;
    }
    return 675000UL / tachometer;
}

static uint16_t read_tachometer(uint8_t high_reg, uint8_t low_reg)
{
    return ((uint16_t)it86x_hwm_read(high_reg) << 8) | it86x_hwm_read(low_reg);
}

static void copy_fan(struct ugreenctl_fan_status *fan, const char *id,
                     uint8_t control_reg, uint8_t pwm_reg,
                     uint8_t tach_high_reg, uint8_t tach_low_reg)
{
    uint16_t tachometer = read_tachometer(tach_high_reg, tach_low_reg);
    (void)snprintf(fan->id, sizeof(fan->id), "%s", id);
    fan->pwm = it86x_hwm_read(pwm_reg);
    fan->manual = (it86x_hwm_read(control_reg) & 0x80) == 0;
    fan->tachometer = tachometer;
    fan->rpm = tach_to_rpm(tachometer);
}

int it8613_read_fans(bool force, struct ugreenctl_fan_status *fans, size_t *fan_count,
                     char *error, size_t error_size)
{
    struct it86x_device device;
    int result = it86x_open(&device, force, error, error_size);

    if (result != 0) {
        return result;
    }
    copy_fan(&fans[0], "cpu", 0x16, 0x6b, 0x19, 0x0e);
    copy_fan(&fans[1], "sys", 0x17, 0x73, 0x1a, 0x0f);
    *fan_count = 2;
    it86x_close(&device);
    return 0;
}

int it8613_set_fan_pwm(bool force, const char *fan_id, uint8_t pwm,
                       char *error, size_t error_size)
{
    struct it86x_device device;
    uint8_t control_reg;
    uint8_t pwm_reg;
    uint8_t control;
    int result;

    if (strcmp(fan_id, "cpu") == 0) {
        control_reg = 0x16;
        pwm_reg = 0x6b;
    } else if (strcmp(fan_id, "sys") == 0) {
        control_reg = 0x17;
        pwm_reg = 0x73;
    } else {
        (void)snprintf(error, error_size, "unknown fan '%s' (expected cpu or sys)", fan_id);
        return -EINVAL;
    }
    if (pwm < 40 && !force) {
        (void)snprintf(error, error_size,
                       "refusing unsafe PWM %u; use a value of 40-255 or add --force", pwm);
        return -EPERM;
    }

    result = it86x_open(&device, force, error, error_size);
    if (result != 0) {
        return result;
    }
    control = it86x_hwm_read(control_reg);
    it86x_hwm_write(control_reg, control & 0x7f);
    it86x_hwm_write(pwm_reg, pwm);
    it86x_close(&device);
    return 0;
}
