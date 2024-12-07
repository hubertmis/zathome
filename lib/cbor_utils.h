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
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#ifdef __cplusplus
extern "C" {
#endif

int cbor_decode_dec_frac_num(zcbor_state_t *cd, int exp, int *value);
int cbor_encode_dec_frac_num(zcbor_state_t *ce, int exp, int value);

int cbor_extract_from_map_string(zcbor_state_t *unordered_map, const char *key, char *value, size_t value_len);
int cbor_extract_from_map_int(zcbor_state_t *unordered_map, const char *key, int *value);
int cbor_extract_from_map_bool(zcbor_state_t *unordered_map, const char *key, bool *value);

#ifdef __cplusplus
}   
#endif

#endif // CBOR_UTILS_H_
