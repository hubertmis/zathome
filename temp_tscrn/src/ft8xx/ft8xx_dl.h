/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief FT8XX memory map
 */

#ifndef ZEPHYR_DRIVERS_DISPLAY_FT8XX_FT8XX_DL_H_
#define ZEPHYR_DRIVERS_DISPLAY_FT8XX_FT8XX_DL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLEAR(c, s, t) (0x26000000 | ((c) ? 0x04 : 0) | ((s) ? 0x02 : 0) | ((t) ? 0x01 : 0))
#define CLEAR_COLOR_RGB(red, green, blue) (0x02000000 | (((uint32_t)(red) & 0xff) << 16) | (((uint32_t)(green) & 0xff) << 8) | ((uint32_t)(blue) & 0xff))
#define COLOR_RGB(red, green, blue) (0x04000000 | (((uint32_t)(red) & 0xff) << 16) | (((uint32_t)(green) & 0xff) << 8) | ((uint32_t)(blue) & 0xff))
#define DISPLAY() 0
#define TAG(s) (0x03000000 | (uint8_t)(s))

#ifdef __cplusplus
}
#endif

#endif // ZEPHYR_DRIVERS_DISPLAY_FT8XX_FT8XX_DL_H_
