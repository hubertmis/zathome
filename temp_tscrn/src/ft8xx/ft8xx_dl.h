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

#define BITMAPS      1U
#define POINTS       2U
#define LINES        3U
#define LINE_STRIP   4U
#define EDGE_STRIP_R 5U
#define EDGE_STRIP_L 6U
#define EDGE_STRIP_A 7U
#define EDGE_STRIP_B 8U
#define RECTS        9U

#define BEGIN(prim) (0x1f000000 | ((prim) & 0x0f))
#define CLEAR(c, s, t) (0x26000000 | ((c) ? 0x04 : 0) | ((s) ? 0x02 : 0) | ((t) ? 0x01 : 0))
#define CLEAR_COLOR_RGB(red, green, blue) (0x02000000 | (((uint32_t)(red) & 0xff) << 16) | (((uint32_t)(green) & 0xff) << 8) | ((uint32_t)(blue) & 0xff))
#define COLOR_RGB(red, green, blue) (0x04000000 | (((uint32_t)(red) & 0xff) << 16) | (((uint32_t)(green) & 0xff) << 8) | ((uint32_t)(blue) & 0xff))
#define DISPLAY() 0
#define END() 0x21000000
#define LINE_WIDTH(width) (0x0e000000 | ((uint32_t)(width) & 0xfff))
#define TAG(s) (0x03000000 | (uint8_t)(s))
#define VERTEX2F(x, y) (0x40000000 | (((int32_t)(x) & 0x7fff) << 15) | ((int32_t)(y) & 0x7fff))
#define VERTEX2II(x, y, handle, cell) (0x80000000 | (((uint32_t)(x) & 0x01ff) << 21) | (((uint32_t)(y) & 0x01ff) << 12) | (((uint32_t)(handle) & 0x1f) << 7) | ((uint32_t)(cell) & 0x7f))

#ifdef __cplusplus
}
#endif

#endif // ZEPHYR_DRIVERS_DISPLAY_FT8XX_FT8XX_DL_H_
