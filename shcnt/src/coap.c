/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap.h"

#include "mot_cnt.h"
#include "mot_cnt_map.h"

#include <coap_fota.h>
#include <coap_sd.h>
#include <coap_server.h>
#include "prov.h"

#include <net/socket.h>
#include <net/coap.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>

#define COAP_PORT 5683
#define COAPS_PORT 5684
#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 64

#define RSRC0_KEY "r0"
#define RSRC1_KEY "r1"
#define DUR0_KEY "d0"
#define DUR1_KEY "d1"

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

    // Handle rsrc0
    cbor_error = cbor_value_map_find_value(&value, RSRC0_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_text_string(&map_val)) {
        size_t str_len;

        cbor_error = cbor_value_get_string_length(&map_val, &str_len);
        if ((cbor_error == CborNoError) && (str_len < PROV_LBL_MAX_LEN)) {
            char str[PROV_LBL_MAX_LEN];
            str_len = PROV_LBL_MAX_LEN;

            cbor_error = cbor_value_copy_text_string(&map_val, str, &str_len, NULL);
            if (cbor_error == CborNoError) {
                r = prov_set_rsrc_label(0, str);

                if (r == 0) {
                    updated = true;
                }
            }
        }
    }

    // Handle rsrc1
    cbor_error = cbor_value_map_find_value(&value, RSRC1_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_text_string(&map_val)) {
        size_t str_len;

        cbor_error = cbor_value_get_string_length(&map_val, &str_len);
        if ((cbor_error == CborNoError) && (str_len < PROV_LBL_MAX_LEN)) {
            char str[PROV_LBL_MAX_LEN];
            str_len = PROV_LBL_MAX_LEN;

            cbor_error = cbor_value_copy_text_string(&map_val, str, &str_len, NULL);
            if (cbor_error == CborNoError) {
                r = prov_set_rsrc_label(1, str);

                if (r == 0) {
                    updated = true;
                }
            }
        }
    }

    // Handle duration 0
    cbor_error = cbor_value_map_find_value(&value, DUR0_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_integer(&map_val)) {
        int value;

        cbor_error = cbor_value_get_int_checked(&map_val, &value);
        if ((cbor_error == CborNoError) && (value >= 0)) {
            r = prov_set_rsrc_duration(0, value);

            if (r == 0) {
                updated = true;
            }
        }
    }

    // Handle duration 1
    cbor_error = cbor_value_map_find_value(&value, DUR1_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_integer(&map_val)) {
        int value;

        cbor_error = cbor_value_get_int_checked(&map_val, &value);
        if ((cbor_error == CborNoError) && (value >= 0)) {
            r = prov_set_rsrc_duration(1, value);

            if (r == 0) {
                updated = true;
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
    int duration;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 4) != CborNoError) return -EINVAL;

    label = prov_get_rsrc_label(0);
    if (cbor_encode_text_string(&map, RSRC0_KEY, strlen(RSRC0_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&map, label, strlen(label)) != CborNoError) return -EINVAL;

    label = prov_get_rsrc_label(1);
    if (cbor_encode_text_string(&map, RSRC1_KEY, strlen(RSRC1_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&map, label, strlen(label)) != CborNoError) return -EINVAL;

    duration = prov_get_rsrc_duration(0);
    if (cbor_encode_text_string(&map, DUR0_KEY, strlen(DUR0_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, duration) != CborNoError) return -EINVAL;

    duration = prov_get_rsrc_duration(1);
    if (cbor_encode_text_string(&map, DUR1_KEY, strlen(DUR1_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, duration) != CborNoError) return -EINVAL;

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

#define VAL_KEY "val"
#define VAL_MIN "down"
#define VAL_MAX "up"
#define VAL_STOP "stop"
#define VAL_LABEL_MAX_LEN 5

static int rsrc_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len, int mot_id)
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

    // Handle val
    cbor_error = cbor_value_map_find_value(&value, VAL_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_text_string(&map_val)) {
        size_t str_len;

        cbor_error = cbor_value_get_string_length(&map_val, &str_len);
        if ((cbor_error == CborNoError) && (str_len < VAL_LABEL_MAX_LEN)) {
            char str[VAL_LABEL_MAX_LEN];
            str_len = VAL_LABEL_MAX_LEN;

            cbor_error = cbor_value_copy_text_string(&map_val, str, &str_len, NULL);
            if (cbor_error == CborNoError) {
                if (strncmp(str, VAL_STOP, strlen(VAL_STOP)) == 0) {
                    const struct device *mot_cnt = mot_cnt_map_from_id(mot_id);
                    const struct mot_cnt_api *api = mot_cnt->api;
                    int ret = api->stop(mot_cnt);

                    if (ret == 0) updated = true;
                } else if (strncmp(str, VAL_MAX, strlen(VAL_MAX)) == 0) {
                    const struct device *mot_cnt = mot_cnt_map_from_id(mot_id);
                    const struct mot_cnt_api *api = mot_cnt->api;
                    int ret = api->max(mot_cnt);

                    if (ret == 0) updated = true;
                } else if (strncmp(str, VAL_MIN, strlen(VAL_MIN)) == 0) {
                    const struct device *mot_cnt = mot_cnt_map_from_id(mot_id);
                    const struct mot_cnt_api *api = mot_cnt->api;
                    int ret = api->min(mot_cnt);

                    if (ret == 0) updated = true;
                }
            }
        }
    } else if ((cbor_error == CborNoError) && cbor_value_is_integer(&map_val)) {
        int value;

        cbor_error = cbor_value_get_int_checked(&map_val, &value);
        if ((cbor_error == CborNoError) && (value >= 0)) {
            const struct device *mot_cnt = mot_cnt_map_from_id(mot_id);
            const struct mot_cnt_api *api = mot_cnt->api;
            int ret = api->go_to(mot_cnt, value);

            if (ret == 0) updated = true;
        }
    }

    if (updated) {
        rsp_code = COAP_RESPONSE_CODE_CHANGED;
    } else {
	    rsp_code = COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT;
    }

    r = coap_server_send_ack(sock, addr, addr_len, id, rsp_code, token, tkl);
    return r;
}

static int prepare_rsrc_payload(uint8_t *payload, size_t len, int id)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;
    const struct device *mot_cnt = mot_cnt_map_from_id(id);
    const struct mot_cnt_api *api = mot_cnt->api;
    int value = api->get_pos(mot_cnt);

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 1) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, VAL_KEY, strlen(VAL_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, value) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int rsrc_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len, int mc_id)
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

    r = prepare_rsrc_payload(payload, MAX_COAP_PAYLOAD_LEN, mc_id);
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

static int rsrc0_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
	return rsrc_get(resource, request, addr, addr_len, 0);
}

static int rsrc0_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
	return rsrc_post(resource, request, addr, addr_len, 0);
}

static int rsrc1_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
	return rsrc_get(resource, request, addr, addr_len, 1);
}

static int rsrc1_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
	return rsrc_post(resource, request, addr, addr_len, 1);
}

static struct coap_resource * rsrcs_get(int sock)
{
    static const char * const fota_path [] = {"fota_req", NULL};
    static const char * const sd_path [] = {"sd", NULL};
    static const char * const prov_path[] = {"prov", NULL};
    static const char * rsrc0_path[] = {NULL, NULL};
    static const char * rsrc1_path[] = {NULL, NULL};

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
	{ .get = rsrc0_get,
	  .post = rsrc0_post,
          .path = rsrc0_path,
	},
	{ .get = rsrc1_get,
	  .post = rsrc1_post,
          .path = rsrc1_path,
	},
        { .path = NULL } // Array terminator
    };

    rsrc0_path[0] = prov_get_rsrc_label(0);
    rsrc1_path[0] = prov_get_rsrc_label(1);

    if (!rsrc1_path[0] || !strlen(rsrc1_path[0])) {
	    resources[4].path = NULL;
    } else {
	    resources[4].path = rsrc1_path;
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
