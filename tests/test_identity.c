#define _POSIX_C_SOURCE 200809L
#include "core/identity.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void expect(int condition, const char *message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static void put(const char *root, const char *name, const void *data, size_t length)
{
    char path[512], *p;
    FILE *stream;
    expect(snprintf(path, sizeof(path), "%s/%s", root, name) < (int)sizeof(path), "fixture path fits");
    for (p = path + 1; *p; ++p) if (*p == '/') {
        *p = 0; expect(mkdir(path, 0700) == 0 || errno == EEXIST, "create fixture parent"); *p = '/';
    }
    stream = fopen(path, "wb"); expect(stream != NULL, "open fixture");
    expect(fwrite(data, 1, length, stream) == length, "write fixture");
    expect(fclose(stream) == 0, "close fixture");
}
#define TEXT(root, name, value) put(root, name, value, sizeof(value) - 1)

static void remove_fixture(const char *root, const char *name)
{
    char path[512];
    expect(snprintf(path, sizeof(path), "%s/%s", root, name) < (int)sizeof(path), "fixture path fits");
    expect(unlink(path) == 0, "remove fixture");
}

static int identity_empty(const struct ugreenctl_machine_identity *identity)
{
    const struct ugreenctl_machine_identity empty = {0};
    return memcmp(identity, &empty, sizeof(empty)) == 0;
}

int main(void)
{
    char root[] = "/tmp/ugreenctl-identity-XXXXXX", error[256], oversized[160];
    const char *bios_fields[] = {"class/dmi/id/bios_vendor", "class/dmi/id/bios_version", "class/dmi/id/bios_date"};
    size_t i;
    struct ugreenctl_machine_identity id;
    expect(mkdtemp(root) != NULL, "create identity fixture");
    TEXT(root, "firmware/devicetree/base/model", "UGREEN RK3576 DH2300 Board\0");
    TEXT(root, "firmware/devicetree/base/compatible", "rockchip,rk3576-evb\0rockchip,rk3576\0");
    expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) == 0, "read real-shaped DT identity without DMI");
    expect(!strcmp(id.source, "devicetree") && !strcmp(id.product_name, "UGREEN RK3576 DH2300 Board"), "preserve raw DT identity");
    expect(!strcmp(ugreenctl_identity_model(&id), "DH2300"), "exact proven DT alias");
    expect(ugreenctl_identity_has_compatible(&id, "rockchip,rk3576"), "match exact compatible token");
    expect(!ugreenctl_identity_has_compatible(&id, "rk3576") && !ugreenctl_identity_has_compatible(&id, "rockchip,rk357"), "reject compatible substrings");
    TEXT(root, "firmware/devicetree/base/compatible", "rockchip,rk3588\0");
    expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) == 0 &&
           !strcmp(ugreenctl_identity_model(&id), "UGREEN RK3576 DH2300 Board"), "wrong SoC never grants alias");
    TEXT(root, "firmware/devicetree/base/model", "UGREEN RK3588C DH4300Plus Board\0");
    expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) == 0 &&
           !strcmp(ugreenctl_identity_model(&id), "DH4300 Plus"), "second exact ARM identity");
    TEXT(root, "firmware/devicetree/base/model", "UGREEN RK3588C DH2300Plus Board\0");
    expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) == 0 &&
           !strcmp(ugreenctl_identity_model(&id), id.product_name), "similar unproven board never aliases");
    TEXT(root, "firmware/devicetree/base/compatible", "rockchip,rk3588");
    expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) < 0 && !id.product_name[0], "reject unterminated compatible without partial identity");
    TEXT(root, "firmware/devicetree/base/compatible", "rockchip,rk3588\0\0");
    expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) < 0, "reject empty compatible token");
    TEXT(root, "class/dmi/id/product_name", "DXP4800 Plus\n");
    TEXT(root, "class/dmi/id/product_version", "rev1\n");
    expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) == 0 &&
           !strcmp(id.source, "dmi") && !strcmp(id.product_name, "DXP4800 Plus") && !strcmp(id.product_version, "rev1"), "DMI takes priority over malformed DT");
    TEXT(root, "class/dmi/id/bios_vendor", "American Megatrends International, LLC.\n");
    TEXT(root, "class/dmi/id/bios_version", "1.23\r\n");
    TEXT(root, "class/dmi/id/bios_date", "10/26/2026\n");
    expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) == 0 &&
           !strcmp(id.bios_vendor, "American Megatrends International, LLC.") &&
           !strcmp(id.bios_version, "1.23") && !strcmp(id.bios_date, "10/26/2026"), "read and trim optional DMI BIOS identity");
    for (i = 0; i < sizeof(bios_fields) / sizeof(bios_fields[0]); ++i) remove_fixture(root, bios_fields[i]);
    expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) == 0 &&
           !id.bios_vendor[0] && !id.bios_version[0] && !id.bios_date[0], "missing BIOS identity remains unknown without stale values");
    memset(oversized, 'X', sizeof(oversized));
    for (i = 0; i < sizeof(bios_fields) / sizeof(bios_fields[0]); ++i) {
        TEXT(root, "class/dmi/id/bios_vendor", "BIOS vendor\n");
        TEXT(root, "class/dmi/id/bios_version", "1.23\n");
        TEXT(root, "class/dmi/id/bios_date", "10/26/2026\n");
        expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) == 0, "prime BIOS identity before invalid read");
        TEXT(root, bios_fields[i], "bad\0identity\n");
        expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) == -EBADMSG &&
               identity_empty(&id), "reject embedded NUL in BIOS field without partial or stale identity");
        put(root, bios_fields[i], oversized, sizeof(oversized));
        expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) == -EOVERFLOW &&
               identity_empty(&id), "reject oversized BIOS field without partial identity");
        remove_fixture(root, bios_fields[i]);
    }
    TEXT(root, "class/dmi/id/bios_vendor", "BIOS vendor\n");
    TEXT(root, "class/dmi/id/bios_version", "1.23\n");
    TEXT(root, "class/dmi/id/bios_date", "10/26/2026\n");
    TEXT(root, "class/dmi/id/product_name", "Unknown OEM\n");
    expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) == 0 &&
           !strcmp(ugreenctl_identity_model(&id), "Unknown OEM"), "unknown DMI does not fall back to ARM aliases");
    TEXT(root, "class/dmi/id/product_name", "DXP4800\0 Plus\n");
    expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) < 0 && !id.source[0], "reject embedded NUL in DMI");
    memset(oversized, 'X', sizeof(oversized));
    put(root, "class/dmi/id/product_name", oversized, sizeof(oversized));
    expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) == -EOVERFLOW && !id.product_name[0], "reject truncated DMI without fallback");
    TEXT(root, "class/dmi/id/product_name", "\n");
    TEXT(root, "firmware/devicetree/base/model", "UGREEN RK3576 DH2300 Board\0");
    TEXT(root, "firmware/devicetree/base/compatible", "rockchip,rk3576\0");
    expect(ugreenctl_identity_read_at(root, &id, error, sizeof(error)) == 0 &&
           !strcmp(ugreenctl_identity_model(&id), "DH2300") &&
           !id.bios_vendor[0] && !id.bios_version[0] && !id.bios_date[0], "DT fallback leaves BIOS identity unknown even with DMI BIOS files present");
    puts("Machine identity safety tests passed");
    return 0;
}
