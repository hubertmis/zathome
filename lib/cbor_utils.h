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

#include <stdbool.h>
#include <tinycbor/cbor.h>

#ifdef __cplusplus
extern "C" {
#endif

int cbor_decode_dec_frac_num(CborValue *cbor_val, int exp, int *value);
int cbor_encode_dec_frac_num(CborEncoder *cbor_enc, int exp, int value);

int cbor_extract_from_map_string(CborValue *map, const char *key, char *value, size_t value_len);
int cbor_extract_from_map_int(CborValue *map, const char *key, int *value);
int cbor_extract_from_map_bool(CborValue *map, const char *key, bool *value);

#ifdef __cplusplus
}   
#endif

#endif // CBOR_UTILS_H_
