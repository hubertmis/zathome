/*
 * Copyright (c) 2024 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Connection to shade controllers
 */

#ifndef SHADES_CONN_H_
#define SHADES_CONN_H_

#include "data_dispatcher.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const char *shades_conn_ids[DATA_SHADE_ID_NUM];

void shades_conn_init(void);
void shades_conn_enable_polling(void);
void shades_conn_disable_polling(void);

#ifdef __cplusplus
}
#endif

#endif // SHADES_CONN_H_
