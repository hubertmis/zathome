/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief FT8XX coprocessor functions
 */

#ifndef ZEPHYR_DRIVERS_DISPLAY_FT8XX_FT8XX_COPRO_H_
#define ZEPHYR_DRIVERS_DISPLAY_FT8XX_FT8XX_COPRO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define cmd(COMMAND)  ft8xx_copro_cmd(COMMAND)
#define cmd_dlstart   ft8xx_copro_cmd_dlstart
#define cmd_swap      ft8xx_copro_cmd_swap
#define cmd_text      ft8xx_copro_cmd_text
#define cmd_number    ft8xx_copro_cmd_number
#define cmd_calibrate ft8xx_copro_cmd_calibrate

#define OPT_3D        0
#define OPT_RGB565    0
#define OPT_MONO      1
#define OPT_NODL      2
#define OPT_FLAT      256
#define OPT_SIGNED    256
#define OPT_CENTERX   512
#define OPT_CENTERY   1024
#define OPT_CENTER    1536
#define OPT_RIGHTX    2048
#define OPT_NOBACK    4096
#define OPT_NOTICKS   8192
#define OPT_NOHM      16384
#define OPT_NOPOINTER 16384
#define OPT_NOSECS    32768
#define OPT_NOHANDS   49152

void ft8xx_copro_cmd(uint32_t cmd);
void ft8xx_copro_cmd_dlstart(void);
void ft8xx_copro_cmd_swap(void);
void ft8xx_copro_cmd_text(int16_t x,
                          int16_t y,
                          int16_t font,
                          uint16_t options,
                          const char *s);
void ft8xx_copro_cmd_number(int16_t x,
                            int16_t y,
                            int16_t font,
                            uint16_t options,
                            int32_t n);
void ft8xx_copro_cmd_calibrate(uint32_t *result);

#ifdef __cplusplus
}
#endif

#endif // ZEPHYR_DRIVERS_DISPLAY_FT8XX_FT8XX_COPRO_H_
