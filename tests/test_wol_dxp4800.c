#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "network/wol_dxp4800.h"

static const char * const *captured_interfaces;
static size_t captured_interface_count;
static enum ugreenctl_wol_policy captured_policy;

static void fail(const char *message)
{
    (void)fprintf(stderr, "FAIL: %s\n", message);
    exit(EXIT_FAILURE);
}

int ugreenctl_wol_read_policy(const char * const *interfaces, size_t interface_count,
                              enum ugreenctl_wol_policy *policy,
                              char *error, size_t error_size)
{
    (void)error;
    (void)error_size;
    captured_interfaces = interfaces;
    captured_interface_count = interface_count;
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
    captured_policy = policy;
    return 0;
}

static void expect_interfaces(void)
{
    if (captured_interface_count != 2 || strcmp(captured_interfaces[0], "eth0") != 0 ||
        strcmp(captured_interfaces[1], "eth1") != 0) {
        fail("DXP4800 Wake-on-LAN interface map");
    }
}

int main(void)
{
    enum ugreenctl_wol_policy policy = UGREENCTL_WOL_UNKNOWN;
    char error[256] = {0};

    if (dxp4800_read_wol_policy(&policy, error, sizeof(error)) != 0 ||
        policy != UGREENCTL_WOL_ON) {
        fail("DXP4800 Wake-on-LAN read route");
    }
    expect_interfaces();
    if (dxp4800_set_wol_policy(UGREENCTL_WOL_OFF, error, sizeof(error)) != 0 ||
        captured_policy != UGREENCTL_WOL_OFF) {
        fail("DXP4800 Wake-on-LAN write route");
    }
    expect_interfaces();

    (void)puts("DXP4800 Wake-on-LAN map tests passed");
    return EXIT_SUCCESS;
}
