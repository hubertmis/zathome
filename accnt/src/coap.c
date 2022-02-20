/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap.h"

#include <coap_fota.h>
#include <coap_sd.h>
#include <coap_server.h>
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

#define BIN_KEY "bin"
#define RSP_EXP_KEY "ersp"

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
    uint16_t payload_len = 0;
    const uint8_t *req_payload;
    uint16_t req_payload_len;

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
    }

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

    if (payload_len) {
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
    bool updated = false;
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
    if ((cbor_error == CborNoError) && cbor_value_is_byte_string(&map_val)) {
        cbor_error = cbor_value_copy_byte_string(&map_val, req, &req_len, NULL);
	if (cbor_error == CborNoError) {

	    ret = duart_tx(req, req_len);
	    if (ret < 0) {
                rsp_code = COAP_RESPONSE_CODE_INTERNAL_ERROR;
                goto end;
	    }

            updated = true;

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

    if (updated) {
        rsp_code = COAP_RESPONSE_CODE_CHANGED;
    }

end:
    ret = coap_server_send_ack(sock, addr, addr_len, id, rsp_code, token, tkl);
    return ret;
}

static struct coap_resource * rsrcs_get(int sock)
{
    static const char * const fota_path [] = {"fota_req", NULL};
    static const char * const sd_path [] = {"sd", NULL};
    static const char * const prov_path[] = {"prov", NULL};
    static const char * rsrc_path[] = {NULL, NULL};

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
        { .path = NULL } // Array terminator
    };

    rsrc_path[0] = prov_get_rsrc_label();

    if (!rsrc_path[0] || !strlen(rsrc_path[0])) {
	    resources[3].path = NULL;
    } else {
	    resources[3].path = rsrc_path;
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
