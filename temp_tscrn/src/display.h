/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Display controller
 */

#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void display_init(void);

void display_debug(int32_t value);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_H_
