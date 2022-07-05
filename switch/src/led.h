/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LED manager settings
 */
    
#ifndef LED_H_
#define LED_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void led_init(void);
void led_set_pulses(uint32_t pulses);

void led_take_analog_control(void);
void led_release_analog_control(void);
void led_analog_toggle(void);

#ifdef __cplusplus
}   
#endif

#endif // LED_H_
