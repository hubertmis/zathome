/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cbor_utils.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

int cbor_decode_dec_frac_num(zcbor_state_t *cd, int exp, int *value)
{
    int32_t rcv_exp;
    int32_t rcv_integer;

    uint32_t tag;

    if (zcbor_tag_decode(cd, &tag)) {
        if (tag != ZCBOR_TAG_DECFRAC_ARR) return -EINVAL;

        if (!zcbor_list_start_decode(cd)) return -EINVAL;
        if (!zcbor_int32_decode(cd, &rcv_exp)) return -EINVAL;
        if (!zcbor_int32_decode(cd, &rcv_integer)) return -EINVAL;
        if (!zcbor_list_end_decode(cd)) return -EINVAL;
    } else if (zcbor_int32_decode(cd, &rcv_integer)) {
        rcv_exp = 0;
    } else {
        // TODO: Handle float ?
        return -EINVAL;
    }

    for (int i = rcv_exp; i < exp; ++i) {
        rcv_integer /= 10;
    }
    for (int i = rcv_exp; i > exp; --i) {
        if ((rcv_integer > INT_MAX / 10) || (rcv_integer < INT_MIN / 10)) {
            return -EINVAL;
        }
        rcv_integer *= 10;
    }

    *value = rcv_integer;
    return 0;
}

int cbor_encode_dec_frac_num(zcbor_state_t *ce, int exp, int value)
{
    // TODO: If exp >= 0, simply encode integer
    // TODO: If exp < 0 AND value % 10^-exp == 0, simply encode integer
    if (!zcbor_tag_put(ce, ZCBOR_TAG_DECFRAC_ARR)) return -EINVAL;
    if (!zcbor_list_start_encode(ce, 2)) return -EINVAL;
    if (!zcbor_int32_put(ce, exp)) return -EINVAL;
    if (!zcbor_int32_put(ce, value)) return -EINVAL;
    if (!zcbor_list_end_encode(ce, 2)) return -EINVAL;

    return 0;
}

int cbor_extract_from_map_string(zcbor_state_t *unordered_map, const char *key, char *value, size_t value_len)
{
    struct zcbor_string str;
    if (!zcbor_search_key_tstr_term(unordered_map, key, 32)) return -EINVAL;
    if (!zcbor_tstr_decode(unordered_map, &str)) return -EINVAL;

    if (str.len >= value_len) return -EINVAL;
    strncpy(value, str.value, value_len);
    value[value_len - 1] = '\0';

    return str.len;
}

int cbor_extract_from_map_int(zcbor_state_t *unordered_map, const char *key, int *value)
{
    if (!zcbor_search_key_tstr_term(unordered_map, key, 32)) return -EINVAL;
    if (!zcbor_int32_decode(unordered_map, value)) return -EINVAL;
    return 0;
}

int cbor_extract_from_map_bool(zcbor_state_t *unordered_map, const char *key, bool *value)
{
    if (!zcbor_search_key_tstr_term(unordered_map, key, 32)) return -EINVAL;
    if (!zcbor_bool_decode(unordered_map, value)) return -EINVAL;
    return 0;
}
