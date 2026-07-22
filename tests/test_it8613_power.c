#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "power/it8613_power.h"
#include "superio/it86x.h"

static uint8_t registers[UINT8_MAX + 1U];
static bool drop_f4_writes;

static void fail(const char *message)
{
    (void)fprintf(stderr, "FAIL: %s\n", message);
    exit(EXIT_FAILURE);
}

int it86x_open(struct it86x_device *device, char *error, size_t error_size)
{
    (void)error;
    (void)error_size;
    memset(device, 0, sizeof(*device));
    return 0;
}

void it86x_close(struct it86x_device *device)
{
    (void)device;
}

uint8_t it86x_sio_read(uint8_t reg)
{
    return registers[reg];
}

void it86x_sio_write(uint8_t reg, uint8_t value)
{
    if (!(drop_f4_writes && reg == 0xf4U)) {
        registers[reg] = value;
    }
}

int main(void)
{
    enum ugreenctl_startup_policy policy = UGREENCTL_STARTUP_UNKNOWN;
    char error[256] = {0};

    registers[0xf2] = 0x00;
    registers[0xf4] = 0x20;
    if (it8613_read_startup_policy(false, &policy, error, sizeof(error)) != 0 ||
        policy != UGREENCTL_STARTUP_ON) {
        fail("AC-recovery on decoding");
    }
    registers[0xf2] = 0xa0;
    registers[0xf4] = 0x00;
    if (it8613_set_startup_policy(false, UGREENCTL_STARTUP_OFF,
                                  error, sizeof(error)) != 0 ||
        registers[0xf2] != 0x80 || registers[0xf4] != 0x60) {
        fail("AC-recovery off write and readback");
    }
    registers[0xf2] = 0x00;
    registers[0xf4] = 0x20;
    drop_f4_writes = true;
    if (it8613_set_startup_policy(false, UGREENCTL_STARTUP_RESTORE,
                                  error, sizeof(error)) != -EIO ||
        strstr(error, "readback") == NULL) {
        fail("AC-recovery readback mismatch");
    }

    (void)puts("it8613 power tests passed");
    return EXIT_SUCCESS;
}
