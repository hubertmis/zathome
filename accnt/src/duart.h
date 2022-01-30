/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Daikin S21 UART driver access
 */
    
#ifndef DUART_H_
#define DUART_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DUART_MAX_FRAME_LEN 8

void duart_init(void);
int duart_rx(unsigned char payload[DUART_MAX_FRAME_LEN]);
int duart_tx(const char *payload, size_t payload_len);

#ifdef __cplusplus
}   
#endif

#endif // DUART_H_
