#ifndef UGREENCTL_THERMAL_CURVE_H
#define UGREENCTL_THERMAL_CURVE_H

#include <stdbool.h>
#include <stddef.h>

/* This module contains the software-side parts of the vendor hwmonitor
 * policy.  It deliberately does not access an IT8613 register: callers must
 * use the guarded model plugin write path for every PWM update. */

struct ugreenctl_curve_thresholds {
    unsigned int stop;
    unsigned int start;
    unsigned int mid;
    unsigned int full;
    unsigned int maximum;
};

struct ugreenctl_curve_pwm_points {
    unsigned int idle;
    unsigned int mid;
    unsigned int full;
    unsigned int maximum;
};

struct ugreenctl_thermal_snapshot {
    int cpu_celsius;
    int hdd_celsius;
    int ssd_celsius;
};

/* The vendor monitors can drive the CPU and system channels differently.
 * Keeping that distinction in the policy layer means the daemon never has to
 * infer hardware mappings or bypass a model plugin. */
struct ugreenctl_fan_curve_plan {
    unsigned int cpu_pwm;
    unsigned int system_pwm;
};

struct ugreenctl_fan_curve_config {
    char profile[24];
    unsigned int interval_seconds;
    unsigned int downshift_delay_seconds;
    unsigned int minimum_pwm;
    unsigned int failsafe_pwm;
    bool require_storage_sensor;
    bool allow_unvalidated_writes;
    bool hdd_curve_enabled;
    bool ssd_curve_enabled;
    struct ugreenctl_curve_thresholds cpu;
    struct ugreenctl_curve_thresholds hdd;
    struct ugreenctl_curve_thresholds ssd;
    /* pwm is the shared custom-curve value kept for configuration
     * compatibility. Stock profiles use the two channel point sets below. */
    struct ugreenctl_curve_pwm_points pwm;
    struct ugreenctl_curve_pwm_points system_pwm;
    struct ugreenctl_curve_pwm_points cpu_pwm;
    bool system_cpu_floor_enabled;
    unsigned int system_cpu_floor_start_celsius;
    unsigned int system_cpu_floor_end_celsius;
    unsigned int system_cpu_floor_start_pwm;
    unsigned int system_cpu_floor_end_pwm;
};

void ugreenctl_fan_curve_config_defaults(struct ugreenctl_fan_curve_config *config);
int ugreenctl_fan_curve_load_config(const char *path,
                                    struct ugreenctl_fan_curve_config *config,
                                    char *error, size_t error_size);
int ugreenctl_fan_curve_validate_config(const struct ugreenctl_fan_curve_config *config,
                                        char *error, size_t error_size);
int ugreenctl_read_thermal_snapshot(struct ugreenctl_thermal_snapshot *snapshot,
                                    char *error, size_t error_size);
int ugreenctl_fan_curve_evaluate(const struct ugreenctl_fan_curve_config *config,
                                 const struct ugreenctl_thermal_snapshot *snapshot,
                                 unsigned int *pwm, char *error, size_t error_size);
int ugreenctl_fan_curve_evaluate_plan(const struct ugreenctl_fan_curve_config *config,
                                      const struct ugreenctl_thermal_snapshot *snapshot,
                                      struct ugreenctl_fan_curve_plan *plan,
                                      char *error, size_t error_size);

#endif
