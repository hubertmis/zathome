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

int cbor_encode_dec_frac_num(CborEncoder *cbor_enc, int exp, int value)
{
    CborError err;
    CborEncoder arr;

    // TODO: If exp >= 0, simply encode integer
    // TODO: If exp < 0 AND value % 10^-exp == 0, simply encode integer
    if ((err = cbor_encode_tag(cbor_enc, TAG_DECIMAL_FRACTION)) != CborNoError) return err;
    if ((err = cbor_encoder_create_array(cbor_enc, &arr, 2)) != CborNoError) return err;
    if ((err = cbor_encode_int(&arr, exp)) != CborNoError) return err;
    if ((err = cbor_encode_int(&arr, value)) != CborNoError) return err;
    if ((err = cbor_encoder_close_container(cbor_enc, &arr)) != CborNoError) return err;

    return CborNoError;
}

int cbor_extract_from_map_string(CborValue *map, const char *key, char *value, size_t value_len)
{
    CborValue map_value;
    CborError cbor_error;

    cbor_error = cbor_value_map_find_value(map, key, &map_value);
    if ((cbor_error == CborNoError) && cbor_value_is_text_string(&map_value)) {
        cbor_error = cbor_value_copy_text_string(&map_value, value, &value_len, NULL);
        if (cbor_error == CborNoError) {
            return value_len;
	}
    }

    return -EINVAL;
}

int cbor_extract_from_map_int(CborValue *map, const char *key, int *value)
{
    CborValue map_value;
    CborError cbor_error;

    cbor_error = cbor_value_map_find_value(map, key, &map_value);
    if ((cbor_error == CborNoError) && cbor_value_is_integer(&map_value)) {
        cbor_error = cbor_value_get_int(&map_value, value);
        if (cbor_error == CborNoError) {
            return 0;
	}
    }

    return -EINVAL;
}
