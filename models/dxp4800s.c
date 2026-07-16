#include "ugreenctl.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "fan/it8613_dxp4800s.h"
#include "power/it8613_power.h"

static const char * const product_names[] = {
    "DXP4800S",
    NULL
};

static int require_experimental_override(const struct ugreenctl_request *request,
                                         char *error, size_t error_size)
{
    if (request->force) {
        return 0;
    }
    (void)snprintf(error, error_size,
                   "DXP4800S writes are firmware-reversed but await physical validation; "
                   "use --force together with --apply on an exact DXP4800S");
    return -EPERM;
}

static int read_status(const struct ugreenctl_request *request,
                       struct ugreenctl_status *status,
                       char *error, size_t error_size)
{
    int result;

    (void)request;
    memset(status, 0, sizeof(*status));
    (void)snprintf(status->controller, sizeof(status->controller), "ITE IT8613 Super I/O");
    result = it8613_dxp4800s_read_fan(status->fans, &status->fan_count,
                                      error, error_size);
    if (result != 0) {
        return result;
    }
    return it8613_read_startup_policy(false, &status->startup_policy,
                                      error, error_size);
}

static int set_fan_pwm(const struct ugreenctl_request *request,
                       const char *fan_id, uint8_t pwm,
                       char *error, size_t error_size)
{
    int result = require_experimental_override(request, error, error_size);
    if (result != 0) {
        return result;
    }
    return it8613_dxp4800s_set_fan_pwm(fan_id, pwm, error, error_size);
}

static int set_startup_policy(const struct ugreenctl_request *request,
                              enum ugreenctl_startup_policy policy,
                              char *error, size_t error_size)
{
    int result = require_experimental_override(request, error, error_size);
    if (result != 0) {
        return result;
    }
    return it8613_set_startup_policy(false, policy, error, error_size);
}

const struct ugreenctl_plugin *ugreenctl_plugin_v2(void)
{
    static const struct ugreenctl_plugin plugin = {
        .abi_version = UGREENCTL_PLUGIN_ABI_V2,
        .id = "dxp4800s",
        .display_name = "UGREEN DXP4800S (firmware-reversed)",
        .dmi_product_names = product_names,
        .capabilities = UGREENCTL_CAP_FAN | UGREENCTL_CAP_POWER,
        .read_status = read_status,
        .set_fan_pwm = set_fan_pwm,
        .set_startup_policy = set_startup_policy,
        .set_led = NULL
    };
    return &plugin;
}
