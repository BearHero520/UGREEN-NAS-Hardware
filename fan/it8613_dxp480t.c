#include "fan/it8613_dxp480t.h"

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

static void copy_cpu_fan(struct ugreenctl_fan_status *fan)
{
    uint16_t tachometer = read_tachometer(0x1a, 0x0f);

    (void)snprintf(fan->id, sizeof(fan->id), "cpu");
    fan->pwm = it86x_hwm_read(0x6b);
    fan->pwm_known = true;
    fan->manual = (it86x_hwm_read(0x16) & 0x80) == 0;
    fan->mode_known = true;
    fan->tachometer = tachometer;
    fan->rpm = tach_to_rpm(tachometer);
}

static void copy_rpm_only_fan(struct ugreenctl_fan_status *fan, const char *id,
                              uint8_t high_reg, uint8_t low_reg)
{
    uint16_t tachometer = read_tachometer(high_reg, low_reg);

    (void)snprintf(fan->id, sizeof(fan->id), "%s", id);
    fan->pwm = 0;
    fan->pwm_known = false;
    fan->manual = false;
    fan->mode_known = false;
    fan->tachometer = tachometer;
    fan->rpm = tach_to_rpm(tachometer);
}

static void set_manual_pwm(uint8_t control_reg, uint8_t pwm_reg, uint8_t pwm)
{
    uint8_t control = it86x_hwm_read(control_reg);
    it86x_hwm_write(control_reg, control & 0x7f);
    it86x_hwm_write(pwm_reg, pwm);
}

int it8613_dxp480t_read_fans(struct ugreenctl_fan_status *fans, size_t *fan_count,
                              char *error, size_t error_size)
{
    struct it86x_device device;
    int result = it86x_open(&device, false, error, error_size);

    if (result != 0) {
        return result;
    }
    copy_cpu_fan(&fans[0]);
    copy_rpm_only_fan(&fans[1], "sys1", 0x19, 0x0e);
    copy_rpm_only_fan(&fans[2], "sys2", 0x81, 0x80);
    *fan_count = 3;
    it86x_close(&device);
    return 0;
}

int it8613_dxp480t_set_fan_pwm(const char *fan_id, uint8_t pwm,
                                char *error, size_t error_size)
{
    struct it86x_device device;
    int result;

    if (pwm < 40) {
        (void)snprintf(error, error_size,
                       "refusing unsafe PWM %u; choose a value of 40-255", pwm);
        return -EPERM;
    }
    if (strcmp(fan_id, "cpu") != 0 && strcmp(fan_id, "all") != 0) {
        (void)snprintf(error, error_size,
                       "unknown DXP480T fan target '%s' (expected cpu or all)", fan_id);
        return -EINVAL;
    }
    result = it86x_open(&device, false, error, error_size);
    if (result != 0) {
        return result;
    }

    if (strcmp(fan_id, "cpu") == 0) {
        set_manual_pwm(0x16, 0x6b, pwm);
    } else {
        /*
         * Exact write order used by the vendor's DXP480T Plus 'set <PWM>'
         * command: CPU, system fan 2, then system fan 1.
         */
        set_manual_pwm(0x16, 0x6b, pwm);
        set_manual_pwm(0x1e, 0x7b, pwm);
        set_manual_pwm(0x17, 0x73, pwm);
    }
    it86x_close(&device);
    return 0;
}
