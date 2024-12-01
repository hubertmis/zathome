/*
 * Copyright (c) 2024 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Device Firmware Update utilities
 */

#ifndef DFU_UTILS_H_
#define DFU_UTILS_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool dfu_utils_keep_checking_conectivity_until(int64_t uptime);

#ifdef __cplusplus
}
#endif

#endif // DFU_UTILS_H_
