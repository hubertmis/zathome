/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Relay output manager
 */

#ifndef OUTPUT_H_
#define OUTPUT_H_

#ifdef __cplusplus
extern "C" {
#endif

void output_init(void);
void output_relay_toggle(void);

#ifdef __cplusplus
}
#endif

#endif // OUTPUT_H_
