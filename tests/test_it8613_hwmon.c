#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fan/it8613_hwmon.h"

static void fail(const char *message)
{
    (void)fprintf(stderr, "FAIL: %s\n", message);
    exit(EXIT_FAILURE);
}

static void write_file(const char *path, const char *value)
{
    FILE *file = fopen(path, "w");
    if (file == NULL || fputs(value, file) == EOF || fclose(file) != 0) {
        fail(path);
    }
}

static unsigned long read_value(const char *path)
{
    FILE *file = fopen(path, "r");
    unsigned long value;
    if (file == NULL || fscanf(file, "%lu", &value) != 1 || fclose(file) != 0) {
        fail(path);
    }
    return value;
}

static void make_directory(const char *path)
{
    if (mkdir(path, 0700) != 0) {
        fail(path);
    }
}

static void join(char *path, size_t path_size, const char *directory, const char *name)
{
    if (snprintf(path, path_size, "%s/%s", directory, name) >= (int)path_size) {
        fail("path too long");
    }
}

int main(void)
{
    char root[] = "/tmp/ugreenctl-hwmon-test-XXXXXX";
    char hwmon3[PATH_MAX];
    char hwmon27[PATH_MAX];
    char lock[PATH_MAX];
    char path[PATH_MAX];
    char error[256] = {0};
    struct ugreenctl_fan_status fans[3];
    size_t fan_count = 0;
    static const struct it8613_hwmon_channel status_channels[] = {
        {"cpu", 2, 3}, {"sys1", 3, 2}, {"sys2", 4, 4}
    };
    static const struct it8613_hwmon_channel write_channels[] = {
        {"cpu", 2, 3}, {"sys2", 4, 4}, {"sys1", 3, 2}
    };

    if (mkdtemp(root) == NULL) {
        fail("mkdtemp");
    }
    join(hwmon3, sizeof(hwmon3), root, "hwmon3");
    join(hwmon27, sizeof(hwmon27), root, "hwmon27");
    join(lock, sizeof(lock), root, "it8613.lock");
    make_directory(hwmon3);
    make_directory(hwmon27);
    join(path, sizeof(path), hwmon3, "name");
    write_file(path, "coretemp\n");
    join(path, sizeof(path), hwmon27, "name");
    write_file(path, "it8613\n");

    join(path, sizeof(path), hwmon27, "pwm2");
    write_file(path, "120\n");
    join(path, sizeof(path), hwmon27, "pwm2_enable");
    write_file(path, "1\n");
    join(path, sizeof(path), hwmon27, "fan3_input");
    write_file(path, "1125\n");
    join(path, sizeof(path), hwmon27, "pwm3");
    write_file(path, "100\n");
    join(path, sizeof(path), hwmon27, "pwm3_enable");
    write_file(path, "2\n");
    join(path, sizeof(path), hwmon27, "fan2_input");
    write_file(path, "900\n");
    join(path, sizeof(path), hwmon27, "pwm4");
    write_file(path, "100\n");
    join(path, sizeof(path), hwmon27, "pwm4_enable");
    write_file(path, "1\n");
    join(path, sizeof(path), hwmon27, "fan4_input");
    write_file(path, "850\n");

    if (setenv("UGREENCTL_HWMON_ROOT", root, 1) != 0 ||
        setenv("UGREENCTL_IT8613_LOCK", lock, 1) != 0) {
        fail("setenv");
    }
    if (it8613_hwmon_read_fans(status_channels, 3, fans, &fan_count,
                                error, sizeof(error)) != 0) {
        (void)fprintf(stderr, "%s\n", error);
        fail("dynamic hwmon read");
    }
    if (fan_count != 3 || strcmp(fans[0].id, "cpu") != 0 ||
        fans[0].pwm != 120 || fans[0].rpm != 1125 || !fans[0].manual ||
        strcmp(fans[1].id, "sys1") != 0 || fans[1].mode_known ||
        strcmp(fans[2].id, "sys2") != 0 || fans[2].rpm != 850) {
        fail("mapped fan status");
    }
    if (it8613_hwmon_set_manual_pwm(write_channels, 3, 39,
                                     error, sizeof(error)) == 0) {
        fail("unsafe PWM accepted");
    }
    if (it8613_hwmon_set_manual_pwm(write_channels, 3, 140,
                                     error, sizeof(error)) != 0) {
        (void)fprintf(stderr, "%s\n", error);
        fail("manual PWM write");
    }
    join(path, sizeof(path), hwmon27, "pwm2");
    if (read_value(path) != 140) fail("pwm2 readback");
    join(path, sizeof(path), hwmon27, "pwm4");
    if (read_value(path) != 140) fail("pwm4 readback");
    join(path, sizeof(path), hwmon27, "pwm3");
    if (read_value(path) != 140) fail("pwm3 readback");
    join(path, sizeof(path), hwmon27, "pwm3_enable");
    if (read_value(path) != 1) fail("manual enable readback");

    (void)puts("it8613 hwmon tests passed");
    return EXIT_SUCCESS;
}
