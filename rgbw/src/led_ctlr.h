/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LED orchestrator
 */
    
#ifndef LED_CTLR_H_
#define LED_CTLR_H_

#include "led.h"

#ifdef __cplusplus
extern "C" {
#endif

void led_ctlr_init(void);

int led_ctlr_set_auto(const struct leds_brightness *leds);
int led_ctlr_set_manual(const struct leds_brightness *leds, unsigned anim_dur_ms, unsigned long validity_ms);
int led_ctlr_reset_manual(void);
int led_ctlr_dim(unsigned long validity_ms);
int led_ctlr_reset_dimmer(void);

#ifdef __cplusplus
}   
#endif

#endif // LED_CTLR_H_

