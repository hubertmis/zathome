/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LED animation support
 */
    
#ifndef LED_H_
#define LED_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "prov.h"

#define MAX_BRIGHTNESS 255U

typedef struct prov_leds_brightness leds_brightness;

bool leds_brightness_equal(const leds_brightness *a,
                           const leds_brightness *b);

void led_init(void);

int led_get(leds_brightness *leds);

/** @brief Set LEDs to given color instantly
 */
int led_set(const leds_brightness *leds);
/** @brief Set LEDs to given color gradually
 */
int led_anim(const leds_brightness *leds, unsigned dur_ms);

#ifdef __cplusplus
}   
#endif

#endif // LED_H_
