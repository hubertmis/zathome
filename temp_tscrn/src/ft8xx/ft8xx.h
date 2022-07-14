/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief FT8XX public API
 */

#ifndef ZEPHYR_DRIVERS_DISPLAY_FT8XX_FT8XX_H_
#define ZEPHYR_DRIVERS_DISPLAY_FT8XX_FT8XX_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ft8xx_touch_transform {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
};

typedef void (*int_callback)(void);

void ft8xx_calibrate(struct ft8xx_touch_transform *data);
void ft8xx_touch_transform_set(const struct ft8xx_touch_transform *data);

int ft8xx_get_touch_tag(void);
void ft8xx_register_int(int_callback callback);
uint32_t ft8xx_get_tracker_value(void);

#ifdef __cplusplus
}
#endif

#endif // ZEPHYR_DRIVERS_DISPLAY_FT8XX_FT8XX_H_
