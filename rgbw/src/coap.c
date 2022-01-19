/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap.h"

#include <errno.h>
#include <stdint.h>

#include <coap_fota.h>
#include <coap_sd.h>
#include <coap_server.h>
#include "data_dispatcher.h"
#include "led.h"
#include "preset.h"
#include "prov.h"

#include <net/socket.h>
#include <net/coap.h>
#include <net/fota_download.h>
#include <net/tls_credentials.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>
#include <zephyr.h>

#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 64

#define COAP_CONTENT_FORMAT_TEXT 0
#define COAP_CONTENT_FORMAT_CBOR 60

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
    uint8_t token[8];
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

    if (coap_option_value_to_int(&option) != COAP_CONTENT_FORMAT_CBOR) {
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
    uint8_t token[8];
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
            COAP_CONTENT_FORMAT_CBOR);
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

#define RED_KEY "r"
#define GREEN_KEY "g"
#define BLUE_KEY "b"
#define WHITE_KEY "w"
#define PRESET_KEY "p"

static int rgb_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[8];
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

    if (coap_option_value_to_int(&option) != COAP_CONTENT_FORMAT_CBOR) {
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
    unsigned r, g, b, w;
    unsigned p;

    if (led_get(&r, &g, &b, &w) != 0) return -EINVAL;

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

    // Handle red
    cbor_error = cbor_value_map_find_value(&value, RED_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_integer(&map_val)) {
        if ((cbor_value_get_int(&map_val, &r) == CborNoError) && (r <= 100)) {
	    updated = true;
        }
    }

    // Handle green
    cbor_error = cbor_value_map_find_value(&value, GREEN_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_integer(&map_val)) {
        if ((cbor_value_get_int(&map_val, &g) == CborNoError) && (g <= 100)) {
	    updated = true;
        }
    }

    // Handle blue
    cbor_error = cbor_value_map_find_value(&value, BLUE_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_integer(&map_val)) {
        if ((cbor_value_get_int(&map_val, &b) == CborNoError) && (b <= 100)) {
	    updated = true;
        }
    }

    // Handle white
    cbor_error = cbor_value_map_find_value(&value, WHITE_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_integer(&map_val)) {
        if ((cbor_value_get_int(&map_val, &w) == CborNoError) && (w <= 100)) {
	    updated = true;
        }
    }

    // Handle preset
    cbor_error = cbor_value_map_find_value(&value, PRESET_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_integer(&map_val)) {
        if (cbor_value_get_int(&map_val, &p) == CborNoError) {
	    ret = preset_get(p, &r, &g, &b, &w);
	    if (!ret) {
		    updated = true;
	    }
        }
    }

    if (updated) {
        rsp_code = COAP_RESPONSE_CODE_CHANGED;
        led_set(r, g, b, w);
    }

    ret = coap_server_send_ack(sock, addr, addr_len, id, rsp_code, token, tkl);
    return ret;
}

static int prepare_rgb_payload(uint8_t *payload, size_t len)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;
    unsigned r, g, b, w;

    if (led_get(&r, &g, &b, &w) != 0) return -EINVAL;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 4) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, RED_KEY, strlen(RED_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_uint(&map, r) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, GREEN_KEY, strlen(GREEN_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_uint(&map, g) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, BLUE_KEY, strlen(BLUE_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_uint(&map, b) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, WHITE_KEY, strlen(WHITE_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_uint(&map, w) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int rgb_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[8];
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
            COAP_CONTENT_FORMAT_CBOR);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload_marker(&response);
    if (r < 0) {
        goto end;
    }

    r = prepare_rgb_payload(payload, MAX_COAP_PAYLOAD_LEN);
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

static struct coap_resource * rsrcs_get(int sock)
{
    static const char * const fota_path [] = {"fota_req", NULL};
    static const char * const sd_path [] = {"sd", NULL};
    static const char * const prov_path[] = {"prov", NULL};
    static const char * const rgb_path[] = {"rgb", NULL};
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
	{ .get = rgb_get,
	  .post = rgb_post,
	  .path = rgb_path,
	},
	{ .get = rgb_get,
	  .post = rgb_post,
          .path = rsrc_path,
	},
        { .path = NULL } // Array terminator
    };

    rsrc_path[0] = prov_get_rsrc_label();

    if (!rsrc_path[0] || !strlen(rsrc_path[0])) {
	    resources[4].path = NULL;
    } else {
	    resources[4].path = rsrc_path;
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
