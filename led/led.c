#include "led/led.h"

#include <errno.h>
#include <stdio.h>

int ugreenctl_led_not_implemented(char *error, size_t error_size)
{
    (void)snprintf(error, error_size,
                   "no verified LED register map is available for this model");
    return -ENOTSUP;
}
