#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fan/it8613_direct.h"
#include "superio/it86x.h"

static uint8_t registers[UINT8_MAX + 1U];
static int mock_open_result;
static unsigned int open_calls;
static unsigned int close_calls;

static void fail(const char *message)
{
    (void)fprintf(stderr, "FAIL: %s\n", message);
    exit(EXIT_FAILURE);
}

int it86x_open(struct it86x_device *device, char *error, size_t error_size)
{
    ++open_calls;
    memset(device, 0, sizeof(*device));
    device->lock_fd = -1;
    if (mock_open_result != 0 && error != NULL && error_size > 0) {
        (void)snprintf(error, error_size, "mock direct-open failure");
    }
    return mock_open_result;
}

void it86x_close(struct it86x_device *device)
{
    ++close_calls;
    memset(device, 0, sizeof(*device));
    device->lock_fd = -1;
}

uint8_t it86x_hwm_read(uint8_t reg)
{
    return registers[reg];
}

void it86x_hwm_write(uint8_t reg, uint8_t value)
{
    registers[reg] = value;
}

int main(void)
{
    static const struct it8613_direct_channel channel = {
        "sys", 0x17, 0x73, 0x0f, 0x1a
    };
    struct ugreenctl_fan_status fan;
    char error[256] = {0};
    size_t fan_count = 0;

    registers[0x17] = 0x00;
    registers[0x73] = 84;
    registers[0x0f] = 0xee;
    registers[0x1a] = 0x02;
    if (it8613_direct_read_fans(&channel, 1, &fan, &fan_count,
                                error, sizeof(error)) != 0) {
        (void)fprintf(stderr, "%s\n", error);
        fail("direct status read");
    }
    if (fan_count != 1 || strcmp(fan.id, "sys") != 0 || !fan.pwm_known ||
        fan.pwm != 84 || !fan.manual || !fan.mode_known ||
        fan.tachometer != 750 || fan.rpm != 900 ||
        open_calls != 1 || close_calls != 1) {
        fail("mapped direct status");
    }

    registers[0x0f] = 0xff;
    registers[0x1a] = 0xff;
    if (it8613_direct_read_fans(&channel, 1, &fan, &fan_count,
                                error, sizeof(error)) != 0 || fan.rpm != 0) {
        fail("invalid tachometer handling");
    }

    if (it8613_direct_set_manual_pwm(&channel, 1, 39,
                                     error, sizeof(error)) != -EPERM ||
        open_calls != 2) {
        fail("unsafe direct PWM guard");
    }

    registers[0x17] = 0x80;
    registers[0x73] = 255;
    if (it8613_direct_set_manual_pwm(&channel, 1, 120,
                                     error, sizeof(error)) != 0 ||
        (registers[0x17] & 0x80U) != 0 || registers[0x73] != 120 ||
        open_calls != 3 || close_calls != 3) {
        fail("manual direct PWM write and readback");
    }

    mock_open_result = -EBUSY;
    if (it8613_direct_read_fans(&channel, 1, &fan, &fan_count,
                                error, sizeof(error)) != -EBUSY ||
        strstr(error, "mock direct-open failure") == NULL) {
        fail("direct owner failure propagation");
    }

    (void)puts("it8613 direct tests passed");
    return EXIT_SUCCESS;
}
