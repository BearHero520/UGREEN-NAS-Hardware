#include "ugreenctl.h"

#include <stdio.h>
#include <string.h>

#include "fan/it8613_fan.h"
#include "power/it8613_power.h"

static const char * const product_names[] = {
    "DXP4800 Plus",
    "DXP4800 Pro",
    NULL
};

static int read_status(const struct ugreenctl_request *request,
                       struct ugreenctl_status *status,
                       char *error, size_t error_size)
{
    int result;

    memset(status, 0, sizeof(*status));
    (void)snprintf(status->controller, sizeof(status->controller), "ITE IT8613 Super I/O");
    result = it8613_read_fans(request->force, status->fans, &status->fan_count,
                              error, error_size);
    if (result != 0) {
        return result;
    }
    return it8613_read_startup_policy(request->force, &status->startup_policy,
                                      error, error_size);
}

static int set_fan_pwm(const struct ugreenctl_request *request,
                       const char *fan_id, uint8_t pwm,
                       char *error, size_t error_size)
{
    return it8613_set_fan_pwm(request->force, fan_id, pwm, error, error_size);
}

static int set_startup_policy(const struct ugreenctl_request *request,
                              enum ugreenctl_startup_policy policy,
                              char *error, size_t error_size)
{
    return it8613_set_startup_policy(request->force, policy, error, error_size);
}

const struct ugreenctl_plugin *ugreenctl_plugin_v1(void)
{
    static const struct ugreenctl_plugin plugin = {
        .abi_version = UGREENCTL_PLUGIN_ABI_V1,
        .id = "dxp4800plus",
        .display_name = "UGREEN DXP4800 Plus / DXP4800 Pro",
        .dmi_product_names = product_names,
        .capabilities = UGREENCTL_CAP_FAN | UGREENCTL_CAP_POWER,
        .read_status = read_status,
        .set_fan_pwm = set_fan_pwm,
        .set_startup_policy = set_startup_policy,
        .set_led = NULL
    };
    return &plugin;
}
