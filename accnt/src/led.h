/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LED connection status display
 */
    
#ifndef LED_H_
#define LED_H_

#ifdef __cplusplus
extern "C" {
#endif

int led_success(void);
int led_failure(void);

#ifdef __cplusplus
}   
#endif

#endif // LED_H_
