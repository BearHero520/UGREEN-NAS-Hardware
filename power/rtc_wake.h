#ifndef UGREENCTL_RTC_WAKE_H
#define UGREENCTL_RTC_WAKE_H

#include <stddef.h>
#include <time.h>

int ugreenctl_rtc_wake_parse(const char *value, time_t *epoch,
                             char *error, size_t error_size);
int ugreenctl_rtc_wake_read(time_t *epoch, char *error, size_t error_size);
int ugreenctl_rtc_wake_set(time_t epoch, char *error, size_t error_size);
int ugreenctl_rtc_wake_clear(char *error, size_t error_size);

#endif
