/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap.h"

#include <coap_fota.h>
#include <coap_sd.h>
#include <coap_server.h>
#include "ds21.h"
#include "duart.h"
#include "prov.h"

#include <net/socket.h>
#include <net/coap.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>

#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 64

#define RSRC_KEY "r"

static int prov_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[COAP_TOKEN_MAX_LEN];
    int r = 0;
    struct coap_option option;
    const uint8_t *payload;
    uint16_t payload_len;
    enum coap_response_code rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;

    code = coap_header_get_code(request);
    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    if (type != COAP_TYPE_CON) {
        return -EINVAL;
    }

#if BLOCK_GLOBAL_ACCESS
    if (!addr_is_local(addr, addr_len) && !sock_is_secure(sock)) {
        // TODO: Send ACK Forbidden?
        return -EINVAL;
    }
#endif

    r = coap_find_options(request, COAP_OPTION_CONTENT_FORMAT, &option, 1); 
    if (r != 1) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (coap_option_value_to_int(&option) != COAP_CONTENT_FORMAT_APP_CBOR) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT, token, tkl);
        return -EINVAL;
    }

    payload = coap_packet_get_payload(request, &payload_len);
    if (!payload) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    CborError cbor_error;
    CborParser parser;
    CborValue value;
    struct cbor_buf_reader reader;
    bool updated = false;

    cbor_buf_reader_init(&reader, payload, payload_len);

    cbor_error = cbor_parser_init(&reader.r, 0, &parser, &value);
    if (cbor_error != CborNoError) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (!cbor_value_is_map(&value)) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    CborValue map_val;

    // Handle rsrc
    cbor_error = cbor_value_map_find_value(&value, RSRC_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_text_string(&map_val)) {
        size_t str_len;

        cbor_error = cbor_value_get_string_length(&map_val, &str_len);
        if ((cbor_error == CborNoError) && (str_len < PROV_LBL_MAX_LEN)) {
            char str[PROV_LBL_MAX_LEN];
            str_len = PROV_LBL_MAX_LEN;

            cbor_error = cbor_value_copy_text_string(&map_val, str, &str_len, NULL);
            if (cbor_error == CborNoError) {
                r = prov_set_rsrc_label(str);

                if (r == 0) {
                    updated = true;
                }
            }
        }
    }

    if (updated) {
        rsp_code = COAP_RESPONSE_CODE_CHANGED;
        prov_store();
    }

    r = coap_server_send_ack(sock, addr, addr_len, id, rsp_code, token, tkl);
    return r;
}

static int prepare_prov_payload(uint8_t *payload, size_t len)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;
    const char *label;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 1) != CborNoError) return -EINVAL;

    label = prov_get_rsrc_label();
    if (cbor_encode_text_string(&map, RSRC_KEY, strlen(RSRC_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&map, label, strlen(label)) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int prov_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[COAP_TOKEN_MAX_LEN];
    uint8_t *data;
    int r = 0;
    struct coap_packet response;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];

    code = coap_header_get_code(request);
    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    if (type != COAP_TYPE_CON) {
        return -EINVAL;
    }

#if BLOCK_GLOBAL_ACCESS
    if (!addr_is_local(addr, addr_len) && !sock_is_secure(sock)) {
        // TODO: Send ACK Forbidden?
        return -EINVAL;
    }
#endif

    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
    if (!data) {
        return -ENOMEM;
    }

    r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
                 1, COAP_TYPE_ACK, tkl, token,
                 COAP_RESPONSE_CODE_CONTENT, id);
    if (r < 0) {
        goto end;
    }

    r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
            COAP_CONTENT_FORMAT_APP_CBOR);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload_marker(&response);
    if (r < 0) {
        goto end;
    }

    r = prepare_prov_payload(payload, MAX_COAP_PAYLOAD_LEN);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload(&response, payload, r);
    if (r < 0) {
        goto end;
    }

    r = coap_server_send_coap_reply(sock, &response, addr, addr_len);

end:
    k_free(data);

    return r;
}

#define TAG_DECIMAL_FRACTION 4

#define RSP_EXP_KEY "ersp"
#define BIN_KEY "bin"
#define READY_KEY "r"
#define ONOFF_KEY "o"
#define MODE_KEY "m"
#define TEMP_KEY "t"
#define FAN_KEY "f"

#define MODE_DISABLED_VAL '0'
#define MODE_AUTO_VAL 'a'
#define MODE_DRY_VAL 'd'
#define MODE_COOL_VAL 'c'
#define MODE_HEAT_VAL 'h'
#define MODE_FAN_VAL 'f'

#define FAN_AUTO_VAL 'a'
#define FAN_1_VAL '1'
#define FAN_2_VAL '2'
#define FAN_3_VAL '3'
#define FAN_4_VAL '4'
#define FAN_5_VAL '5'

#define TEMP_INT_KEY "i"
#define TEMP_EXT_KEY "e"

static int encode_temp(CborEncoder *enc, int16_t temp)
{
    CborError err;
    CborEncoder arr;

    if ((err = cbor_encode_tag(enc, TAG_DECIMAL_FRACTION)) != CborNoError) return err;
    if ((err = cbor_encoder_create_array(enc, &arr, 2)) != CborNoError) return err;
    if ((err = cbor_encode_negative_int(&arr, 0)) != CborNoError) return err;
    if ((err = cbor_encode_int(&arr, temp)) != CborNoError) return err;
    if ((err = cbor_encoder_close_container(enc, &arr)) != CborNoError) return err;

    return CborNoError;
}

static int decode_temp(CborValue *cbor_val, int16_t *temp)
{
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

        int integer;
        cbor_error = cbor_value_get_int(&frac_arr, &integer);
        if ((cbor_error != CborNoError) || (integer != -1)) {
            return -EINVAL;
        }

        cbor_error = cbor_value_advance_fixed(&frac_arr);
        if ((cbor_error != CborNoError) || !cbor_value_is_integer(&frac_arr)) {
            return -EINVAL;
        }

        cbor_error = cbor_value_get_int(&frac_arr, &integer);
        if (cbor_error != CborNoError) {
            return -EINVAL;
        }

        *temp = integer;
        return 0;
    } else if (cbor_value_is_integer(cbor_val)) {
        int integer;
        cbor_error = cbor_value_get_int(cbor_val, &integer);

        if (cbor_error != CborNoError) {
            return -EINVAL;
        }
        if ((integer > INT16_MAX / 10) || (integer < INT16_MIN / 10)) {
            return -EINVAL;
        }

        *temp = integer * 10;
        return 0;
    } else {
        // TODO: Handle float ?
        return -EINVAL;
    }
}

static int prepare_default_payload(uint8_t *payload, size_t len)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;
    struct ds21_basic_state state;
    int ret;
    char mode;
    char fan;

    ret = ds21_get_basic_state(&state);
    if (ret < 0) return ret;

    switch (state.mode) {
	    case DS21_MODE_DISABLED:
		    mode = MODE_DISABLED_VAL;
		    break;

	    case DS21_MODE_AUTO:
		    mode = MODE_AUTO_VAL;
		    break;

	    case DS21_MODE_DRY:
		    mode = MODE_DRY_VAL;
		    break;

	    case DS21_MODE_COOL:
		    mode = MODE_COOL_VAL;
		    break;

	    case DS21_MODE_HEAT:
		    mode = MODE_HEAT_VAL;
		    break;

	    case DS21_MODE_FAN:
		    mode = MODE_FAN_VAL;
		    break;

	    default:
		    return -EIO;
    }

    switch (state.fan) {
	    case DS21_FAN_AUTO:
		    fan = FAN_AUTO_VAL;
		    break;

	    case DS21_FAN_1:
		    fan = FAN_1_VAL;
		    break;

	    case DS21_FAN_2:
		    fan = FAN_2_VAL;
		    break;

	    case DS21_FAN_3:
		    fan = FAN_3_VAL;
		    break;

	    case DS21_FAN_4:
		    fan = FAN_4_VAL;
		    break;

	    case DS21_FAN_5:
		    fan = FAN_5_VAL;
		    break;

            default:
		    return -EIO;
    }

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 4) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, ONOFF_KEY, strlen(ONOFF_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_boolean(&map, state.enabled) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, MODE_KEY, strlen(MODE_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, mode) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, TEMP_KEY, strlen(TEMP_KEY)) != CborNoError) return -EINVAL;
    if (encode_temp(&map, state.target_temp) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, FAN_KEY, strlen(FAN_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, fan) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int prepare_bytestream_payload(uint8_t *payload, size_t len)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;
    uint8_t rsp[DUART_MAX_FRAME_LEN];
    size_t rsp_len;
    int ret;

    ret = duart_rx(rsp);
    if (ret < 0) return ret;
    rsp_len = ret;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 1) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, BIN_KEY, strlen(BIN_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_byte_string(&map, rsp, rsp_len) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int prepare_bool_payload(uint8_t *payload, size_t len, char *key, size_t key_len, bool value)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 1) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, key, key_len) != CborNoError) return -EINVAL;
    if (cbor_encode_boolean(&map, value) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int rsrc_get(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[COAP_TOKEN_MAX_LEN];
    uint8_t *data;
    int r = 0;
    struct coap_packet response;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    int16_t payload_len = 0;
    const uint8_t *req_payload;
    uint16_t req_payload_len;
    enum coap_response_code rsp_code = COAP_RESPONSE_CODE_INTERNAL_ERROR;

    code = coap_header_get_code(request);
    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    if (type != COAP_TYPE_CON) {
        return -EINVAL;
    }

#if BLOCK_GLOBAL_ACCESS
    if (!addr_is_local(addr, addr_len) && !sock_is_secure(sock)) {
        // TODO: Send ACK Forbidden?
        return -EINVAL;
    }
#endif

    req_payload = coap_packet_get_payload(request, &req_payload_len);
    if (req_payload) {
	    struct coap_option option;
	    r = coap_find_options(request, COAP_OPTION_CONTENT_FORMAT, &option, 1); 
	    if (r != 1) {
		coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
		return -EINVAL;
	    }

	    if (coap_option_value_to_int(&option) != COAP_CONTENT_FORMAT_APP_CBOR) {
		coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT, token, tkl);
		return -EINVAL;
	    }

	    CborError cbor_error;
	    CborParser parser;
	    CborValue value;
	    struct cbor_buf_reader reader;
	    uint8_t req[DUART_MAX_FRAME_LEN];
	    size_t req_len = sizeof(req);

	    cbor_buf_reader_init(&reader, req_payload, req_payload_len);

	    cbor_error = cbor_parser_init(&reader.r, 0, &parser, &value);
	    if (cbor_error != CborNoError) {
		coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
		return -EINVAL;
	    }

	    if (!cbor_value_is_map(&value)) {
		coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
		return -EINVAL;
	    }

	    CborValue map_val;

	    // Handle binary command
	    cbor_error = cbor_value_map_find_value(&value, BIN_KEY, &map_val);
	    if ((cbor_error == CborNoError) && cbor_value_is_byte_string(&map_val)) {
		cbor_error = cbor_value_copy_byte_string(&map_val, req, &req_len, NULL);
		if (cbor_error == CborNoError) {
		    r = duart_tx(req, req_len);
		    if (r < 0) {
			coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_INTERNAL_ERROR, token, tkl);
			return -EINVAL;
		    }

		    r = prepare_bytestream_payload(payload, sizeof(payload));
		    if (r < 0) {
			coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_INTERNAL_ERROR, token, tkl);
			return -EINVAL;
		    }

		    payload_len = r;
		}
	    }

	    // Handle readiness checker
	    if (!payload_len) {
	        cbor_error = cbor_value_map_find_value(&value, READY_KEY, &map_val);
	        if ((cbor_error == CborNoError) && cbor_value_is_boolean(&map_val)) {
		    bool result = false;
	            cbor_error = cbor_value_get_boolean(&map_val, &result);
	            if ((cbor_error == CborNoError) && result) {
			result = ds21_is_ready();

	                r = prepare_bool_payload(payload, sizeof(payload), READY_KEY, sizeof(READY_KEY), result);
	                if (r < 0) {
	                    coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_INTERNAL_ERROR, token, tkl);
	                    return -EINVAL;
	                }

	                payload_len = r;
	            }
	        }
            }
    } else {
	    payload_len = prepare_default_payload(payload, sizeof(payload));
    }

    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
    if (!data) {
        return -ENOMEM;
    }

    if (payload_len >= 0) {
	    rsp_code = COAP_RESPONSE_CODE_CONTENT;
    }

    r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
                 1, COAP_TYPE_ACK, tkl, token,
                 rsp_code, id);
    if (r < 0) {
        goto end;
    }

    if (payload_len > 0) {
	    r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
		    COAP_CONTENT_FORMAT_APP_CBOR);
	    if (r < 0) {
		goto end;
	    }

	    r = coap_packet_append_payload_marker(&response);
	    if (r < 0) {
		goto end;
	    }

	    r = coap_packet_append_payload(&response, payload, payload_len);
	    if (r < 0) {
		goto end;
	    }
    }

    r = coap_server_send_coap_reply(sock, &response, addr, addr_len);

end:
    k_free(data);

    return r;
}

static int rsrc_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[COAP_TOKEN_MAX_LEN];
    int ret = 0;
    struct coap_option option;
    const uint8_t *payload;
    uint16_t payload_len;
    enum coap_response_code rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;

    code = coap_header_get_code(request);
    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    if (type != COAP_TYPE_CON) {
        return -EINVAL;
    }

#if BLOCK_GLOBAL_ACCESS
    if (!addr_is_local(addr, addr_len) && !sock_is_secure(sock)) {
        // TODO: Send ACK Forbidden?
        return -EINVAL;
    }
#endif

    ret = coap_find_options(request, COAP_OPTION_CONTENT_FORMAT, &option, 1); 
    if (ret != 1) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (coap_option_value_to_int(&option) != COAP_CONTENT_FORMAT_APP_CBOR) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT, token, tkl);
        return -EINVAL;
    }

    payload = coap_packet_get_payload(request, &payload_len);
    if (!payload) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    CborError cbor_error;
    CborParser parser;
    CborValue value;
    struct cbor_buf_reader reader;
    uint8_t req[DUART_MAX_FRAME_LEN];
    size_t req_len = sizeof(req);
    bool expect_rsp = false;
    uint8_t rsp_payload[MAX_COAP_PAYLOAD_LEN];
    size_t rsp_payload_len;

    cbor_buf_reader_init(&reader, payload, payload_len);

    cbor_error = cbor_parser_init(&reader.r, 0, &parser, &value);
    if (cbor_error != CborNoError) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (!cbor_value_is_map(&value)) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    CborValue map_val;

    // Handle response expectation
    cbor_error = cbor_value_map_find_value(&value, RSP_EXP_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_boolean(&map_val)) {
        cbor_value_get_boolean(&map_val, &expect_rsp);
    }

    // Handle binary command
    cbor_error = cbor_value_map_find_value(&value, BIN_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_valid(&map_val)) {
        if (cbor_value_is_byte_string(&map_val)) {
            cbor_error = cbor_value_copy_byte_string(&map_val, req, &req_len, NULL);
	    if (cbor_error == CborNoError) {
	        ret = duart_tx(req, req_len);
	        if (ret < 0) {
                    rsp_code = COAP_RESPONSE_CODE_INTERNAL_ERROR;
                    goto end;
	        }

                rsp_code = COAP_RESPONSE_CODE_CHANGED;;

	        if (expect_rsp) {
                    ret = prepare_bytestream_payload(rsp_payload, sizeof(rsp_payload));
	            if (ret < 0) {
                        expect_rsp = false;
                        rsp_code = COAP_RESPONSE_CODE_INTERNAL_ERROR;
                        goto end;
	            }

                    rsp_payload_len = ret;
                    rsp_code = COAP_RESPONSE_CODE_CHANGED;
                    return coap_server_send_ack_with_payload(sock, addr, addr_len, id, rsp_code, token, tkl, rsp_payload, rsp_payload_len);
                }
	    }
        }

	goto end;
    }

    // Handle basic state
    struct ds21_basic_state state;
    bool ds21_basic_state_updated = false;

    // On/off
    cbor_error = cbor_value_map_find_value(&value, ONOFF_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_boolean(&map_val)) {
        if (!ds21_basic_state_updated) {
            ret = ds21_get_basic_state(&state);
            if (ret < 0) {
                rsp_code = COAP_RESPONSE_CODE_INTERNAL_ERROR;
                goto end;
            }
        }

        cbor_error = cbor_value_get_boolean(&map_val, &state.enabled);
        if (cbor_error == CborNoError) {
            ds21_basic_state_updated = true;
        } else {
            rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
	    goto end;
	}
    }

    // Mode
    cbor_error = cbor_value_map_find_value(&value, MODE_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_unsigned_integer(&map_val)) {
        uint64_t val;
        if (!ds21_basic_state_updated) {
            ret = ds21_get_basic_state(&state);
            if (ret < 0) {
                rsp_code = COAP_RESPONSE_CODE_INTERNAL_ERROR;
                goto end;
            }
        }

        cbor_error = cbor_value_get_uint64(&map_val, &val);
        if (cbor_error == CborNoError) {
            switch (val) {
                case MODE_DISABLED_VAL:
                    state.mode= DS21_MODE_DISABLED;
                    break;

                case MODE_AUTO_VAL:
                    state.mode = DS21_MODE_AUTO;
                    break;

                case MODE_DRY_VAL:
                    state.mode = DS21_MODE_DRY;
                    break;

                case MODE_COOL_VAL:
                    state.mode = DS21_MODE_COOL;
                    break;

                case MODE_HEAT_VAL:
                    state.mode = DS21_MODE_HEAT;
                    break;

                case MODE_FAN_VAL:
                    state.mode = DS21_MODE_FAN;
                    break;

                default:
                    rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
                    goto end;
            }

            ds21_basic_state_updated = true;
        }
    }

    // Target temperature
    cbor_error = cbor_value_map_find_value(&value, TEMP_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_valid(&map_val)) {
        if (!ds21_basic_state_updated) {
            ret = ds21_get_basic_state(&state);
            if (ret < 0) {
                rsp_code = COAP_RESPONSE_CODE_INTERNAL_ERROR;
                goto end;
            }
        }

        ret = decode_temp(&map_val, &state.target_temp);
        if (ret < 0) {
            rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
            goto end;
        }

        ds21_basic_state_updated = true;
    }

    // Fan
    cbor_error = cbor_value_map_find_value(&value, FAN_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_unsigned_integer(&map_val)) {
        uint64_t val;
        if (!ds21_basic_state_updated) {
            ret = ds21_get_basic_state(&state);
            if (ret < 0) {
                rsp_code = COAP_RESPONSE_CODE_INTERNAL_ERROR;
                goto end;
            }
        }

        cbor_error = cbor_value_get_uint64(&map_val, &val);
        if (cbor_error == CborNoError) {
            switch (val) {
                case FAN_AUTO_VAL:
                    state.fan = DS21_FAN_AUTO;
                    break;

                case FAN_1_VAL:
                    state.fan = DS21_FAN_1;
                    break;

                case FAN_2_VAL:
                    state.fan = DS21_FAN_2;
                    break;

                case FAN_3_VAL:
                    state.fan = DS21_FAN_3;
                    break;

                case FAN_4_VAL:
                    state.fan = DS21_FAN_4;
                    break;

                case FAN_5_VAL:
                    state.fan = DS21_FAN_5;
                    break;

                default:
                    rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
                    goto end;
            }

            ds21_basic_state_updated = true;
        }
    }

    if (ds21_basic_state_updated) {
        ret = ds21_set_basic_state(&state);
        rsp_code = (ret < 0) ? COAP_RESPONSE_CODE_INTERNAL_ERROR : COAP_RESPONSE_CODE_CHANGED;
    }

end:
    ret = coap_server_send_ack(sock, addr, addr_len, id, rsp_code, token, tkl);
    return ret;
}

static int prepare_temp_payload(uint8_t *payload, size_t len)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;
    struct ds21_temperature temp;
    int ret;

    ret = ds21_get_temperature(&temp);
    if (ret < 0) return ret;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 2) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, TEMP_INT_KEY, strlen(TEMP_INT_KEY)) != CborNoError) return -EINVAL;
    if (encode_temp(&map, temp.internal) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, TEMP_EXT_KEY, strlen(TEMP_EXT_KEY)) != CborNoError) return -EINVAL;
    if (encode_temp(&map, temp.external) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int temp_get(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[COAP_TOKEN_MAX_LEN];
    uint8_t *data;
    int r = 0;
    struct coap_packet response;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    int16_t payload_len = 0;
    enum coap_response_code rsp_code = COAP_RESPONSE_CODE_INTERNAL_ERROR;

    code = coap_header_get_code(request);
    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    if (type != COAP_TYPE_CON) {
        return -EINVAL;
    }

#if BLOCK_GLOBAL_ACCESS
    if (!addr_is_local(addr, addr_len) && !sock_is_secure(sock)) {
        // TODO: Send ACK Forbidden?
        return -EINVAL;
    }
#endif

    payload_len = prepare_temp_payload(payload, sizeof(payload));

    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
    if (!data) {
        return -ENOMEM;
    }

    if (payload_len >= 0) {
	    rsp_code = COAP_RESPONSE_CODE_CONTENT;
    }

    r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
                 1, COAP_TYPE_ACK, tkl, token,
                 rsp_code, id);
    if (r < 0) {
        goto end;
    }

    if (payload_len > 0) {
	    r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
		    COAP_CONTENT_FORMAT_APP_CBOR);
	    if (r < 0) {
		goto end;
	    }

	    r = coap_packet_append_payload_marker(&response);
	    if (r < 0) {
		goto end;
	    }

	    r = coap_packet_append_payload(&response, payload, payload_len);
	    if (r < 0) {
		goto end;
	    }
    }

    r = coap_server_send_coap_reply(sock, &response, addr, addr_len);

end:
    k_free(data);

    return r;
}

#define TEMP_PATH "temp"

static struct coap_resource * rsrcs_get(int sock)
{
    static const char * const fota_path [] = {"fota_req", NULL};
    static const char * const sd_path [] = {"sd", NULL};
    static const char * const prov_path[] = {"prov", NULL};
    static const char * rsrc_path[] = {NULL, NULL};
    static const char * rsrc_temp_path[] = {NULL, TEMP_PATH, NULL};

    static struct coap_resource resources[] = {
        { .get = coap_fota_get,
          .post = coap_fota_post,
          .path = fota_path,
        },
        { .get = coap_sd_server,
          .path = sd_path,
        },
	{ .get = prov_get,
	  .post = prov_post,
	  .path = prov_path,
	},
	{ .get = rsrc_get,
	  .post = rsrc_post,
          .path = rsrc_path,
	},
	{ .get = temp_get,
	  .path = rsrc_temp_path,
	},
        { .path = NULL } // Array terminator
    };

    rsrc_path[0] = prov_get_rsrc_label();
    rsrc_temp_path[0] = rsrc_path[0];

    if (!rsrc_path[0] || !strlen(rsrc_path[0])) {
	    resources[3].path = NULL;
	    resources[4].path = NULL;
    } else {
	    resources[3].path = rsrc_path;
	    resources[4].path = rsrc_temp_path;
    }

    // TODO: Replace it with something better
    static int user_data;
   
    user_data = sock;

    for (int i = 0; i < ARRAY_SIZE(resources); ++i) {
        resources[i].user_data = &user_data;
    }

    return resources;
}

void coap_init(void)
{
    coap_server_init(rsrcs_get);
}
