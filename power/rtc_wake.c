#define _DEFAULT_SOURCE

#include "power/rtc_wake.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int wakealarm_path(char *path, size_t path_size, char *error, size_t error_size)
{
    DIR *directory;
    struct dirent *entry;

    directory = opendir("/sys/class/rtc");
    if (directory == NULL) {
        (void)snprintf(error, error_size, "cannot inspect RTC wake support: %s", strerror(errno));
        return -errno;
    }
    while ((entry = readdir(directory)) != NULL) {
        int length;
        struct stat status;

        if (strncmp(entry->d_name, "rtc", 3) != 0 || entry->d_name[3] == '\0') {
            continue;
        }
        length = snprintf(path, path_size, "/sys/class/rtc/%s/wakealarm", entry->d_name);
        if (length < 0 || (size_t)length >= path_size) {
            (void)closedir(directory);
            (void)snprintf(error, error_size, "RTC wakealarm path is too long");
            return -ENAMETOOLONG;
        }
        if (stat(path, &status) == 0 && S_ISREG(status.st_mode)) {
            (void)closedir(directory);
            return 0;
        }
    }
    (void)closedir(directory);
    (void)snprintf(error, error_size, "no RTC wakealarm interface is available");
    return -ENODEV;
}

static int write_wakealarm(const char *path, const char *value,
                           char *error, size_t error_size)
{
    int fd;
    size_t length = strlen(value);
    ssize_t written;

    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        (void)snprintf(error, error_size, "cannot open RTC wakealarm: %s", strerror(errno));
        return -errno;
    }
    written = write(fd, value, length);
    if (written < 0 || (size_t)written != length) {
        int saved_errno = written < 0 ? errno : EIO;

        (void)close(fd);
        (void)snprintf(error, error_size, "cannot write RTC wakealarm: %s", strerror(saved_errno));
        return -saved_errno;
    }
    (void)close(fd);
    return 0;
}

int ugreenctl_rtc_wake_read(time_t *epoch, char *error, size_t error_size)
{
    char path[PATH_MAX];
    char value[64];
    char *end;
    int fd;
    int result;
    ssize_t count;
    long long parsed;

    if (epoch == NULL) {
        (void)snprintf(error, error_size, "RTC wakealarm output is required");
        return -EINVAL;
    }
    result = wakealarm_path(path, sizeof(path), error, error_size);
    if (result != 0) {
        return result;
    }
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        (void)snprintf(error, error_size, "cannot read RTC wakealarm: %s", strerror(errno));
        return -errno;
    }
    count = read(fd, value, sizeof(value) - 1U);
    (void)close(fd);
    if (count < 0) {
        (void)snprintf(error, error_size, "cannot read RTC wakealarm: %s", strerror(errno));
        return -errno;
    }
    value[count] = '\0';
    errno = 0;
    parsed = strtoll(value, &end, 10);
    if (errno != 0 || end == value || (*end != '\0' && *end != '\n')) {
        (void)snprintf(error, error_size, "RTC wakealarm returned an invalid value");
        return -EIO;
    }
    if (parsed < 0 || (unsigned long long)parsed > (unsigned long long)LLONG_MAX) {
        (void)snprintf(error, error_size, "RTC wakealarm returned an out-of-range value");
        return -ERANGE;
    }
    *epoch = (time_t)parsed;
    return 0;
}

int ugreenctl_rtc_wake_clear(char *error, size_t error_size)
{
    char path[PATH_MAX];
    int result = wakealarm_path(path, sizeof(path), error, error_size);

    if (result != 0) {
        return result;
    }
    result = write_wakealarm(path, "0", error, error_size);
    if (result != 0) {
        return result;
    }
    {
        time_t epoch = -1;

        result = ugreenctl_rtc_wake_read(&epoch, error, error_size);
        if (result != 0) {
            return result;
        }
        if (epoch != 0) {
            (void)snprintf(error, error_size, "RTC wakealarm clear did not read back as disabled");
            return -EIO;
        }
    }
    return 0;
}

int ugreenctl_rtc_wake_set(time_t epoch, char *error, size_t error_size)
{
    char path[PATH_MAX];
    char value[64];
    time_t readback;
    time_t now = time(NULL);
    int result;

    if (now == (time_t)-1 || epoch <= now + 60) {
        (void)snprintf(error, error_size, "RTC wake time must be at least 60 seconds in the future");
        return -EINVAL;
    }
    result = wakealarm_path(path, sizeof(path), error, error_size);
    if (result != 0) {
        return result;
    }
    result = write_wakealarm(path, "0", error, error_size);
    if (result != 0) {
        return result;
    }
    if (snprintf(value, sizeof(value), "%lld", (long long)epoch) >= (int)sizeof(value)) {
        (void)snprintf(error, error_size, "RTC wake time is out of range");
        return -ERANGE;
    }
    result = write_wakealarm(path, value, error, error_size);
    if (result != 0) {
        return result;
    }
    result = ugreenctl_rtc_wake_read(&readback, error, error_size);
    if (result != 0) {
        return result;
    }
    if (readback != epoch) {
        (void)snprintf(error, error_size,
                       "RTC wakealarm readback did not match requested wake time");
        return -EIO;
    }
    return 0;
}
