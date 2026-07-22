#ifndef UGREENCTL_WOL_H
#define UGREENCTL_WOL_H

#include <stddef.h>

#include "ugreenctl.h"

int ugreenctl_wol_read_policy(const char * const *interfaces, size_t interface_count,
                              enum ugreenctl_wol_policy *policy,
                              char *error, size_t error_size);
int ugreenctl_wol_set_policy(const char * const *interfaces, size_t interface_count,
                             enum ugreenctl_wol_policy policy,
                             char *error, size_t error_size);

#endif
