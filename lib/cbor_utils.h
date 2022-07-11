/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Utilities for common CBOR operations
 */
    
#ifndef CBOR_UTILS_H_
#define CBOR_UTILS_H_

#include <tinycbor/cbor.h>

#ifdef __cplusplus
extern "C" {
#endif

int cbor_decode_dec_frac_num(CborValue *cbor_val, int exp, int *value);

#ifdef __cplusplus
}   
#endif

#endif // CBOR_UTILS_H_
