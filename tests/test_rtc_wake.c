#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "power/rtc_wake.h"

static void fail(const char *message)
{
    (void)fprintf(stderr, "FAIL: %s\n", message);
    exit(EXIT_FAILURE);
}

static void expect_value(const char *value, time_t expected)
{
    char error[256] = {0};
    time_t epoch = -1;

    if (ugreenctl_rtc_wake_parse(value, &epoch, error, sizeof(error)) != 0 ||
        epoch != expected) {
        fail("wakealarm value did not parse as expected");
    }
}

int main(void)
{
    char error[256] = {0};
    time_t epoch = -1;

    expect_value("", 0);
    expect_value(" \t\r\n", 0);
    expect_value("1712345678\n", (time_t)1712345678);
    if (ugreenctl_rtc_wake_parse("not-a-time\n", &epoch, error, sizeof(error)) == 0) {
        fail("an invalid wakealarm must be rejected");
    }
    (void)puts("RTC wakealarm parsing tests passed");
    return EXIT_SUCCESS;
}
