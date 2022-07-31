/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Preset LED configurations
 */
    
#ifndef PRESET_H_
#define PRESET_H_

#include "led.h"

#ifdef __cplusplus
extern "C" {
#endif

int preset_get(unsigned preset,
		struct leds_brightness *leds,
		unsigned *duration);

#ifdef __cplusplus
}   
#endif

#endif // PRESET_H_
