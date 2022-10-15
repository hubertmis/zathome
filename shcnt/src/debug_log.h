/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#define DEBUG 0

#if DEBUG
void debug_log(uint32_t value);
uint32_t debug_log_get(uint32_t **log);
#else
#define debug_log(...)
#define debug_log_get(...) 0
#endif
