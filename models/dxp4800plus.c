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

static int read_fans(const struct ugreenctl_request *request,
                     struct ugreenctl_fan_status *fans, size_t *fan_count,
                     char *error, size_t error_size)
{
    return it8613_read_fans(request->force, fans, fan_count, error, error_size);
}

static int read_startup_policy(const struct ugreenctl_request *request,
                               enum ugreenctl_startup_policy *policy,
                               char *error, size_t error_size)
{
    return it8613_read_startup_policy(request->force, policy, error, error_size);
}

/* ABI v2 compatibility keeps a combined read, but startup failure is now
 * non-fatal so an older client can still display and control the fans. */
static int read_status(const struct ugreenctl_request *request,
                       struct ugreenctl_status *status,
                       char *error, size_t error_size)
{
    char startup_error[256] = {0};
    int result;

    memset(status, 0, sizeof(*status));
    (void)snprintf(status->controller, sizeof(status->controller), "ITE IT8613 hwmon");
    result = read_fans(request, status->fans, &status->fan_count, error, error_size);
    if (result != 0) {
        return result;
    }
    status->startup_policy = UGREENCTL_STARTUP_UNKNOWN;
    (void)read_startup_policy(request, &status->startup_policy,
                              startup_error, sizeof(startup_error));
    return 0;
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

const struct ugreenctl_plugin *ugreenctl_plugin_v4(void)
{
    static const struct ugreenctl_plugin plugin = {
        .abi_version = UGREENCTL_PLUGIN_ABI_V4,
        .id = "dxp4800plus",
        .display_name = "UGREEN DXP4800 Plus / DXP4800 Pro",
        .dmi_product_names = product_names,
        .capabilities = UGREENCTL_CAP_FAN | UGREENCTL_CAP_POWER,
        .read_status = read_status,
        .set_fan_pwm = set_fan_pwm,
        .set_startup_policy = set_startup_policy,
        .set_led = NULL,
        .set_fan_mode = NULL,
        .read_fans = read_fans,
        .read_startup_policy = read_startup_policy,
        .controller_name = "ITE IT8613 hwmon"
    };
    return &plugin;
}

const struct ugreenctl_plugin *ugreenctl_plugin_v2(void)
{
    static const struct ugreenctl_plugin plugin = {
        .abi_version = UGREENCTL_PLUGIN_ABI_V2,
        .id = "dxp4800plus",
        .display_name = "UGREEN DXP4800 Plus / DXP4800 Pro",
        .dmi_product_names = product_names,
        .capabilities = UGREENCTL_CAP_FAN | UGREENCTL_CAP_POWER,
        .read_status = read_status,
        .set_fan_pwm = set_fan_pwm,
        .set_startup_policy = set_startup_policy,
        .set_led = NULL,
        .set_fan_mode = NULL
    };
    return &plugin;
}
