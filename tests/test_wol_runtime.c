#define _DEFAULT_SOURCE
#include <assert.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

static FILE *fixture_fopen(const char *, const char *);
static DIR *fixture_opendir(const char *);
static unsigned fixture_if_nametoindex(const char *);
static int fixture_socket(int, int, int);
static int fixture_close(int);
static int fixture_ioctl(int, unsigned long, ...);
#define fopen fixture_fopen
#define opendir fixture_opendir
#define if_nametoindex fixture_if_nametoindex
#define socket fixture_socket
#define close fixture_close
#define ioctl fixture_ioctl
#undef _DEFAULT_SOURCE
#include "network/wol.c"
#undef fopen
#undef opendir
#undef if_nametoindex
#undef socket
#undef close
#undef ioctl

static char root[256];
static unsigned writes, queries, nonmagic, mismatch;
static unsigned enabled[2];
static char written[2][IFNAMSIZ];

static const char *mapped_path(const char *path, char *out, size_t size)
{
    assert(strncmp(path, "/sys/class/net", 14) == 0);
    assert(snprintf(out, size, "%s%s", root, path + 14) < (int)size);
    return out;
}
static FILE *fixture_fopen(const char *path, const char *mode)
{
    char out[512];
    return fopen(mapped_path(path, out, sizeof(out)), mode);
}
static DIR *fixture_opendir(const char *path)
{
    char out[512];
    return opendir(mapped_path(path, out, sizeof(out)));
}
static unsigned fixture_if_nametoindex(const char *name)
{
    char path[512];
    struct stat s;
    snprintf(path, sizeof(path), "%s/%s", root, name);
    return stat(path, &s) == 0 ? 1 : 0;
}
static int fixture_socket(int domain, int type, int protocol)
{
    (void)domain; (void)type; (void)protocol;
    return 42;
}
static int fixture_close(int fd) { assert(fd == 42); return 0; }
static int fixture_ioctl(int fd, unsigned long operation, ...)
{
    va_list args;
    struct ifreq *req;
    struct ethtool_wolinfo *wol;
    unsigned index;
    assert(fd == 42 && operation == SIOCETHTOOL);
    va_start(args, operation);
    req = va_arg(args, struct ifreq *);
    va_end(args);
    assert(strcmp(req->ifr_name, "enp3s0") == 0 || strcmp(req->ifr_name, "eno2") == 0);
    index = strcmp(req->ifr_name, "enp3s0") == 0 ? 0 : 1;
    wol = (struct ethtool_wolinfo *)req->ifr_data;
    if (wol->cmd == ETHTOOL_GWOL) {
        ++queries;
        wol->supported = nonmagic && index == 1 ? 0 : WAKE_MAGIC;
        wol->wolopts = mismatch ? 0 : enabled[index];
    } else {
        assert(wol->cmd == ETHTOOL_SWOL && writes < 2);
        strcpy(written[writes++], req->ifr_name);
        enabled[index] = wol->wolopts;
    }
    return 0;
}
static void add_interface(const char *name, const char *class_value, const char *slot)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s", root, name); assert(mkdir(path, 0700) == 0);
    if (!slot) return; /* bridge, veth, bond and loopback have no PCI device */
    snprintf(path, sizeof(path), "%s/%s/device", root, name); assert(mkdir(path, 0700) == 0);
    snprintf(path, sizeof(path), "%s/%s/device/uevent", root, name);
    f = fopen(path, "w"); assert(f);
    fprintf(f, "PCI_CLASS=%s\nPCI_SLOT_NAME=%s\n", class_value, slot);
    fclose(f);
}
int main(void)
{
    const char *names[] = {"eth0", "eth1"};
    char error[256] = {0};
    enum ugreenctl_wol_policy policy;
    char temporary[] = "/tmp/ugreen-wol-XXXXXX";
    assert(mkdtemp(temporary)); strcpy(root, temporary);
    add_interface("eno2", "020000", "0000:04:00.0");
    add_interface("enp3s0", "020000", "0000:03:00.0");
    add_interface("eth0", NULL, NULL); /* virtual eth0 must not force stock route */
    add_interface("br0", NULL, NULL);
    add_interface("bond0", NULL, NULL);
    add_interface("veth0", NULL, NULL);
    add_interface("wlan0", "028000", "0000:05:00.0");
    assert(ugreenctl_wol_set_policy(names, 2, UGREENCTL_WOL_ON, error, sizeof(error)) == 0);
    assert(writes == 2 && queries == 4);
    assert(strcmp(written[0], "enp3s0") == 0 && strcmp(written[1], "eno2") == 0);
    assert(ugreenctl_wol_read_policy(names, 2, &policy, error, sizeof(error)) == 0);
    assert(policy == UGREENCTL_WOL_ON);
    nonmagic = 1; writes = 0;
    assert(ugreenctl_wol_set_policy(names, 2, UGREENCTL_WOL_OFF, error, sizeof(error)) == -EOPNOTSUPP);
    assert(writes == 0); /* preflight every NIC before the first write */
    nonmagic = 0; mismatch = 1;
    assert(ugreenctl_wol_set_policy(names, 2, UGREENCTL_WOL_ON, error, sizeof(error)) != 0);
    mismatch = 0; writes = 0;
    add_interface("enp6s0", "020000", "0000:06:00.0");
    assert(ugreenctl_wol_set_policy(names, 2, UGREENCTL_WOL_ON, error, sizeof(error)) == -ENODEV);
    assert(writes == 0);
    puts("Renamed physical NIC selection, virtual exclusion, preflight and readback passed");
    return 0;
}
