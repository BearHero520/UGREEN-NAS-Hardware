#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "network/wol_dx4600.h"
#include "network/wol_dxp4800.h"
#include "network/wol_dxp4800plus.h"
#include "network/wol_dxp4800s.h"
#include "network/wol_dxp480tplus.h"
#include "network/wol_dxp6800pro.h"

static const char * const *captured_interfaces;
static size_t captured_interface_count;
static size_t captured_renamed_interface_count;
static enum ugreenctl_wol_policy captured_policy;

int ugreenctl_wol_read_policy(const char * const *interfaces, size_t interface_count,
                              enum ugreenctl_wol_policy *policy,
                              char *error, size_t error_size)
{
    (void)error;
    (void)error_size;
    captured_interfaces = interfaces;
    captured_interface_count = interface_count;
    captured_renamed_interface_count = 0;
    *policy = UGREENCTL_WOL_ON;
    return 0;
}

int ugreenctl_wol_set_policy(const char * const *interfaces, size_t interface_count,
                             enum ugreenctl_wol_policy policy,
                             char *error, size_t error_size)
{
    (void)error;
    (void)error_size;
    captured_interfaces = interfaces;
    captured_interface_count = interface_count;
    captured_renamed_interface_count = 0;
    captured_policy = policy;
    return 0;
}

int ugreenctl_wol_read_policy_with_renamed_count(const char * const *interfaces,
                                                 size_t interface_count,
                                                 size_t renamed_interface_count,
                                                 enum ugreenctl_wol_policy *policy,
                                                 char *error, size_t error_size)
{
    (void)error;
    (void)error_size;
    captured_interfaces = interfaces;
    captured_interface_count = interface_count;
    captured_renamed_interface_count = renamed_interface_count;
    *policy = UGREENCTL_WOL_ON;
    return 0;
}

int ugreenctl_wol_set_policy_with_renamed_count(const char * const *interfaces,
                                                size_t interface_count,
                                                size_t renamed_interface_count,
                                                enum ugreenctl_wol_policy policy,
                                                char *error, size_t error_size)
{
    (void)error;
    (void)error_size;
    captured_interfaces = interfaces;
    captured_interface_count = interface_count;
    captured_renamed_interface_count = renamed_interface_count;
    captured_policy = policy;
    return 0;
}

static void expect_interfaces(const char *model, size_t renamed_interface_count)
{
    if (captured_interface_count != 2 || strcmp(captured_interfaces[0], "eth0") != 0 ||
        strcmp(captured_interfaces[1], "eth1") != 0 ||
        captured_renamed_interface_count != renamed_interface_count) {
        (void)fprintf(stderr, "FAIL: %s Wake-on-LAN interface map\n", model);
        exit(EXIT_FAILURE);
    }
}

typedef int (*wol_read_fn)(enum ugreenctl_wol_policy *policy,
                           char *error, size_t error_size);
typedef int (*wol_write_fn)(enum ugreenctl_wol_policy policy,
                            char *error, size_t error_size);

static void expect_route(const char *model, wol_read_fn read_policy,
                         wol_write_fn write_policy, size_t renamed_interface_count)
{
    enum ugreenctl_wol_policy policy = UGREENCTL_WOL_UNKNOWN;
    char error[256] = {0};

    if (read_policy(&policy, error, sizeof(error)) != 0 || policy != UGREENCTL_WOL_ON) {
        (void)fprintf(stderr, "FAIL: %s Wake-on-LAN read route\n", model);
        exit(EXIT_FAILURE);
    }
    expect_interfaces(model, renamed_interface_count);
    if (write_policy(UGREENCTL_WOL_OFF, error, sizeof(error)) != 0 ||
        captured_policy != UGREENCTL_WOL_OFF) {
        (void)fprintf(stderr, "FAIL: %s Wake-on-LAN write route\n", model);
        exit(EXIT_FAILURE);
    }
    expect_interfaces(model, renamed_interface_count);
}

int main(void)
{
    expect_route("DX4600 family", dx4600_read_wol_policy, dx4600_set_wol_policy, 2);
    expect_route("DXP4800", dxp4800_read_wol_policy, dxp4800_set_wol_policy, 0);
    expect_route("DXP4800 Plus / Pro", dxp4800plus_read_wol_policy,
                 dxp4800plus_set_wol_policy, 0);
    expect_route("DXP4800S", dxp4800s_read_wol_policy, dxp4800s_set_wol_policy, 0);
    expect_route("DXP480T Plus", dxp480tplus_read_wol_policy,
                 dxp480tplus_set_wol_policy, 1);
    expect_route("DXP6800 Pro", dxp6800pro_read_wol_policy,
                 dxp6800pro_set_wol_policy, 0);

    (void)puts("Firmware-mapped Wake-on-LAN map tests passed");
    return EXIT_SUCCESS;
}
