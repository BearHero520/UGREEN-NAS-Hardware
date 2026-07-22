#include "network/wol_dxp480tplus.h"

#include "network/wol.h"

/* The stock hwmonitor-480t route for exact DXP480T Plus sets eth0 and eth1. */
static const char * const interfaces[] = {
    "eth0",
    "eth1"
};

const char * const *dxp480tplus_wol_interfaces(size_t *interface_count)
{
    if (interface_count != NULL) {
        *interface_count = sizeof(interfaces) / sizeof(interfaces[0]);
    }
    return interfaces;
}

int dxp480tplus_read_wol_policy(enum ugreenctl_wol_policy *policy,
                                char *error, size_t error_size)
{
    size_t interface_count;
    const char * const *mapped_interfaces = dxp480tplus_wol_interfaces(&interface_count);

    return ugreenctl_wol_read_policy(mapped_interfaces, interface_count,
                                     policy, error, error_size);
}

int dxp480tplus_set_wol_policy(enum ugreenctl_wol_policy policy,
                               char *error, size_t error_size)
{
    size_t interface_count;
    const char * const *mapped_interfaces = dxp480tplus_wol_interfaces(&interface_count);

    return ugreenctl_wol_set_policy(mapped_interfaces, interface_count,
                                    policy, error, error_size);
}
