/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief FT8XX common functions
 */

#ifndef ZEPHYR_DRIVERS_DISPLAY_FT8XX_FT8XX_COMMON_H_
#define ZEPHYR_DRIVERS_DISPLAY_FT8XX_FT8XX_COMMON_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define wr8 ft8xx_wr8
#define wr16 ft8xx_wr16
#define wr32 ft8xx_wr32
#define rd8 ft8xx_rd8
#define rd16 ft8xx_rd16
#define rd32 ft8xx_rd32

// TODO: functions documentation
void ft8xx_wr8(uint32_t address, uint8_t data);
void ft8xx_wr16(uint32_t address, uint16_t data);
void ft8xx_wr32(uint32_t address, uint32_t data);

uint8_t ft8xx_rd8(uint32_t address);
uint16_t ft8xx_rd16(uint32_t address);
uint32_t ft8xx_rd32(uint32_t address);

#ifdef __cplusplus
}
#endif

#endif // ZEPHYR_DRIVERS_DISPLAY_FT8XX_FT8XX_COMMON_H_

