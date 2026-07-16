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

static void copy_fan(struct ugreenctl_fan_status *fan, const char *id,
                     uint8_t control_reg, uint8_t pwm_reg,
                     uint8_t high_reg, uint8_t low_reg)
{
    uint16_t tachometer = read_tachometer(high_reg, low_reg);

    (void)snprintf(fan->id, sizeof(fan->id), "%s", id);
    fan->pwm = it86x_hwm_read(pwm_reg);
    fan->pwm_known = true;
    fan->manual = (it86x_hwm_read(control_reg) & 0x80) == 0;
    fan->mode_known = true;
    fan->tachometer = tachometer;
    fan->rpm = tach_to_rpm(tachometer);
}

static void set_manual_pwm(uint8_t control_reg, uint8_t pwm_reg, uint8_t pwm)
{
    uint8_t control = it86x_hwm_read(control_reg);
    it86x_hwm_write(control_reg, control & 0x7f);
    it86x_hwm_write(pwm_reg, pwm);
}

static void set_automatic_mode(uint8_t control_reg)
{
    uint8_t control = it86x_hwm_read(control_reg);
    it86x_hwm_write(control_reg, control | 0x80);
}

static int validate_manual_pwm(uint8_t pwm_reg, const char *fan_name,
                               char *error, size_t error_size)
{
    uint8_t pwm = it86x_hwm_read(pwm_reg);
    if (pwm >= 40) {
        return 0;
    }
    (void)snprintf(error, error_size,
                   "refusing manual mode for %s with unsafe current PWM %u; set PWM to 40-255 first",
                   fan_name, pwm);
    return -EPERM;
}

int it8613_dxp480t_read_fans(struct ugreenctl_fan_status *fans, size_t *fan_count,
                              char *error, size_t error_size)
{
    struct it86x_device device;
    int result = it86x_open(&device, error, error_size);

    if (result != 0) {
        return result;
    }
    copy_fan(&fans[0], "cpu", 0x16, 0x6b, 0x1a, 0x0f);
    copy_fan(&fans[1], "sys1", 0x17, 0x73, 0x19, 0x0e);
    copy_fan(&fans[2], "sys2", 0x1e, 0x7b, 0x81, 0x80);
    *fan_count = 3;
    it86x_close(&device);
    return 0;
}

int it8613_dxp480t_set_fan_mode(const char *fan_id, bool automatic,
                                char *error, size_t error_size)
{
    struct it86x_device device;
    int result;

    if (strcmp(fan_id, "cpu") != 0 && strcmp(fan_id, "all") != 0) {
        (void)snprintf(error, error_size,
                       "unknown DXP480T fan target '%s' (expected cpu or all)", fan_id);
        return -EINVAL;
    }
    result = it86x_open(&device, error, error_size);
    if (result != 0) {
        return result;
    }
    if (!automatic) {
        result = validate_manual_pwm(0x6b, "CPU fan", error, error_size);
        if (result == 0 && strcmp(fan_id, "all") == 0) {
            result = validate_manual_pwm(0x7b, "system fan 2", error, error_size);
        }
        if (result == 0 && strcmp(fan_id, "all") == 0) {
            result = validate_manual_pwm(0x73, "system fan 1", error, error_size);
        }
        if (result != 0) {
            it86x_close(&device);
            return result;
        }
    }

    if (automatic) {
        set_automatic_mode(0x16);
        if (strcmp(fan_id, "all") == 0) {
            set_automatic_mode(0x1e);
            set_automatic_mode(0x17);
        }
    } else {
        uint8_t control = it86x_hwm_read(0x16);
        it86x_hwm_write(0x16, control & 0x7f);
        if (strcmp(fan_id, "all") == 0) {
            control = it86x_hwm_read(0x1e);
            it86x_hwm_write(0x1e, control & 0x7f);
            control = it86x_hwm_read(0x17);
            it86x_hwm_write(0x17, control & 0x7f);
        }
    }
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
    result = it86x_open(&device, error, error_size);
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
