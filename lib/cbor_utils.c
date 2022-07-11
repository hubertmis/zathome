/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cbor_utils.h"

#include <errno.h>

#define TAG_DECIMAL_FRACTION 4

int cbor_decode_dec_frac_num(CborValue *cbor_val, int exp, int *value)
{
    int rcv_exp;
    int rcv_integer;

    CborError cbor_error;
    if (cbor_value_is_tag(cbor_val)) {
        CborTag sett_tag;

        cbor_error = cbor_value_get_tag(cbor_val, &sett_tag);
        if ((cbor_error != CborNoError) || (sett_tag != TAG_DECIMAL_FRACTION)) {
            return -EINVAL;
        }

        cbor_error = cbor_value_skip_tag(cbor_val);
        if ((cbor_error != CborNoError) || !cbor_value_is_array(cbor_val)) {
            return -EINVAL;
        }

        size_t arr_len;
        cbor_error = cbor_value_get_array_length(cbor_val, &arr_len);
        if ((cbor_error != CborNoError) || (arr_len != 2)) {
            return -EINVAL;
        }

        CborValue frac_arr;
        cbor_error = cbor_value_enter_container(cbor_val, &frac_arr);
        if ((cbor_error != CborNoError) || !cbor_value_is_integer(&frac_arr)) {
            return -EINVAL;
        }

        cbor_error = cbor_value_get_int(&frac_arr, &rcv_exp);
        if (cbor_error != CborNoError) {
            return -EINVAL;
        }

        cbor_error = cbor_value_advance_fixed(&frac_arr);
        if ((cbor_error != CborNoError) || !cbor_value_is_integer(&frac_arr)) {
            return -EINVAL;
        }

        cbor_error = cbor_value_get_int(&frac_arr, &rcv_integer);
        if (cbor_error != CborNoError) {
            return -EINVAL;
        }
    } else if (cbor_value_is_integer(cbor_val)) {
        cbor_error = cbor_value_get_int(cbor_val, &rcv_integer);
	rcv_exp = 0;

        if (cbor_error != CborNoError) {
            return -EINVAL;
        }
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
