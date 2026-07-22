#define _DEFAULT_SOURCE

#include "network/wol.h"

#include <errno.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static int validate_interfaces(const char * const *interfaces, size_t interface_count,
                               char *error, size_t error_size)
{
    size_t index;

    if (interfaces == NULL || interface_count == 0) {
        (void)snprintf(error, error_size, "no Wake-on-LAN interfaces are mapped for this model");
        return -ENODEV;
    }
    for (index = 0; index < interface_count; ++index) {
        if (interfaces[index] == NULL || interfaces[index][0] == '\0' ||
            strlen(interfaces[index]) >= IFNAMSIZ) {
            (void)snprintf(error, error_size, "invalid Wake-on-LAN interface mapping");
            return -EINVAL;
        }
    }
    return 0;
}

static int query_interface(int fd, const char *interface, struct ethtool_wolinfo *wol,
                           char *error, size_t error_size)
{
    struct ifreq request;

    memset(&request, 0, sizeof(request));
    (void)snprintf(request.ifr_name, sizeof(request.ifr_name), "%s", interface);
    memset(wol, 0, sizeof(*wol));
    wol->cmd = ETHTOOL_GWOL;
    request.ifr_data = (char *)wol;
    if (ioctl(fd, SIOCETHTOOL, &request) != 0) {
        (void)snprintf(error, error_size, "cannot read Wake-on-LAN on %s: %s",
                       interface, strerror(errno));
        return -errno;
    }
    if ((wol->supported & WAKE_MAGIC) == 0U) {
        (void)snprintf(error, error_size,
                       "interface %s does not support magic-packet Wake-on-LAN", interface);
        return -EOPNOTSUPP;
    }
    return 0;
}

int ugreenctl_wol_read_policy(const char * const *interfaces, size_t interface_count,
                              enum ugreenctl_wol_policy *policy,
                              char *error, size_t error_size)
{
    enum ugreenctl_wol_policy observed = UGREENCTL_WOL_UNKNOWN;
    int fd;
    int result;
    size_t index;

    if (policy == NULL) {
        (void)snprintf(error, error_size, "Wake-on-LAN policy output is required");
        return -EINVAL;
    }
    result = validate_interfaces(interfaces, interface_count, error, error_size);
    if (result != 0) {
        return result;
    }
    fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        (void)snprintf(error, error_size, "cannot open Wake-on-LAN control socket: %s",
                       strerror(errno));
        return -errno;
    }
    for (index = 0; index < interface_count; ++index) {
        struct ethtool_wolinfo wol;
        enum ugreenctl_wol_policy current;

        result = query_interface(fd, interfaces[index], &wol, error, error_size);
        if (result != 0) {
            (void)close(fd);
            return result;
        }
        current = (wol.wolopts & WAKE_MAGIC) != 0U ? UGREENCTL_WOL_ON : UGREENCTL_WOL_OFF;
        if (observed == UGREENCTL_WOL_UNKNOWN) {
            observed = current;
        } else if (observed != current) {
            (void)close(fd);
            (void)snprintf(error, error_size,
                           "Wake-on-LAN state is mixed across the model's mapped interfaces");
            return -EUCLEAN;
        }
    }
    (void)close(fd);
    *policy = observed;
    return 0;
}

int ugreenctl_wol_set_policy(const char * const *interfaces, size_t interface_count,
                             enum ugreenctl_wol_policy policy,
                             char *error, size_t error_size)
{
    unsigned int desired;
    int fd;
    int result;
    size_t index;

    if (policy == UGREENCTL_WOL_ON) {
        desired = WAKE_MAGIC;
    } else if (policy == UGREENCTL_WOL_OFF) {
        desired = 0U;
    } else {
        (void)snprintf(error, error_size, "Wake-on-LAN policy must be on or off");
        return -EINVAL;
    }
    result = validate_interfaces(interfaces, interface_count, error, error_size);
    if (result != 0) {
        return result;
    }
    fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        (void)snprintf(error, error_size, "cannot open Wake-on-LAN control socket: %s",
                       strerror(errno));
        return -errno;
    }
    for (index = 0; index < interface_count; ++index) {
        struct ethtool_wolinfo wol;

        result = query_interface(fd, interfaces[index], &wol, error, error_size);
        if (result != 0) {
            (void)close(fd);
            return result;
        }
    }
    for (index = 0; index < interface_count; ++index) {
        struct ethtool_wolinfo wol;
        struct ifreq request;

        memset(&request, 0, sizeof(request));
        (void)snprintf(request.ifr_name, sizeof(request.ifr_name), "%s", interfaces[index]);
        memset(&wol, 0, sizeof(wol));
        wol.cmd = ETHTOOL_SWOL;
        wol.wolopts = desired;
        request.ifr_data = (char *)&wol;
        if (ioctl(fd, SIOCETHTOOL, &request) != 0) {
            (void)snprintf(error, error_size, "cannot set Wake-on-LAN on %s: %s",
                           interfaces[index], strerror(errno));
            (void)close(fd);
            return -errno;
        }
    }
    (void)close(fd);
    result = ugreenctl_wol_read_policy(interfaces, interface_count, &policy,
                                       error, error_size);
    if (result != 0) {
        return result;
    }
    if ((policy == UGREENCTL_WOL_ON ? WAKE_MAGIC : 0U) != desired) {
        (void)snprintf(error, error_size, "Wake-on-LAN readback did not match requested policy");
        return -EIO;
    }
    return 0;
}
