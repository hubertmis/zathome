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

void led_init(void);

/** @brief Set LEDs to given color instantly
 */
int led_set(unsigned red, unsigned green, unsigned blue, unsigned white);

#ifdef __cplusplus
}   
#endif

#endif // LED_H_
