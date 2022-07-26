/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap.h"

#include "mot_cnt.h"
#include "mot_cnt_map.h"
#include "pos_srv.h"

#include <cbor_utils.h>
#include <coap_fota.h>
#include <coap_sd.h>
#include <coap_server.h>
#include "prov.h"

#include <net/socket.h>
#include <net/coap.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>

#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 64

#define RSRC0_KEY "r0"
#define RSRC1_KEY "r1"
#define DUR0_KEY "d0"
#define DUR1_KEY "d1"
#define SW_INT0_KEY "i0"
#define SW_INT1_KEY "i1"

static int handle_prov_post(CborValue *value, 
	       	enum coap_response_code *rsp_code, void *context)
{
    (void)context;

    int r;
    bool updated = false;

    char str[PROV_LBL_MAX_LEN];
    int int_val;

    // Handle rsrc0
    r = cbor_extract_from_map_string(value, RSRC0_KEY, str, sizeof(str));
    if ((r >= 0) && (r < PROV_LBL_MAX_LEN)) {
        r = prov_set_rsrc_label(0, str);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle rsrc1
    r = cbor_extract_from_map_string(value, RSRC1_KEY, str, sizeof(str));
    if ((r >= 0) && (r < PROV_LBL_MAX_LEN)) {
        r = prov_set_rsrc_label(1, str);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle duration 0
    r = cbor_extract_from_map_int(value, DUR0_KEY, &int_val);
    if (!r && int_val >= 0) {
        r = prov_set_rsrc_duration(0, int_val);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle duration 1
    r = cbor_extract_from_map_int(value, DUR1_KEY, &int_val);
    if (!r && int_val >= 0) {
        r = prov_set_rsrc_duration(1, int_val);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle swing interval 0
    r = cbor_extract_from_map_int(value, SW_INT0_KEY, &int_val);
    if (!r && int_val >= 0) {
        r = prov_set_swing_interval(0, int_val);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle swing interval 1
    r = cbor_extract_from_map_int(value, SW_INT1_KEY, &int_val);
    if (!r && int_val >= 0) {
        r = prov_set_swing_interval(1, int_val);

        if (r == 0) {
            updated = true;
        }
    }

    if (updated) {
        *rsp_code = COAP_RESPONSE_CODE_CHANGED;
        prov_store();
    }

    return r;
}

static int prov_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_simple_setter(sock, resource, request, addr, addr_len,
		    handle_prov_post, NULL);
}

static int prepare_prov_payload(uint8_t *payload, size_t len)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;
    const char *label;
    int duration;
    int interval;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 6) != CborNoError) return -EINVAL;

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

    interval = prov_get_swing_interval(0);
    if (cbor_encode_text_string(&map, SW_INT0_KEY, strlen(SW_INT0_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, interval) != CborNoError) return -EINVAL;

    interval = prov_get_swing_interval(1);
    if (cbor_encode_text_string(&map, SW_INT1_KEY, strlen(SW_INT1_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, interval) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int prov_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    int r = 0;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    size_t payload_len;

    r = prepare_prov_payload(payload, MAX_COAP_PAYLOAD_LEN);
    if (r < 0) {
        return r;
    }
    payload_len = r;

    return coap_server_handle_simple_getter(sock, resource, request, addr, addr_len,
                    payload, payload_len);
}

#define VAL_KEY "val"
#define VAL_MIN "up"
#define VAL_MAX "down"
#define VAL_STOP "stop"
#define VAL_LABEL_MAX_LEN 5

static int handle_rsrc_post(CborValue *value,
	       	enum coap_response_code *rsp_code, void *context)
{
    int mot_id = *(int *)context;
    bool updated = false;
    int r;
    char str[VAL_LABEL_MAX_LEN];
    int int_val;

    *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;

    // Handle val
    r = cbor_extract_from_map_string(value, VAL_KEY, str, sizeof(str));
    if ((r >= 0) && (r < VAL_LABEL_MAX_LEN)) {
        if (strncmp(str, VAL_STOP, strlen(VAL_STOP)) == 0) {
	    int ret = pos_srv_req(mot_id, MOT_CNT_STOP);
            if (ret == 0) updated = true;
        } else if (strncmp(str, VAL_MAX, strlen(VAL_MAX)) == 0) {
	    int ret = pos_srv_req(mot_id, MOT_CNT_MAX);
            if (ret == 0) updated = true;
        } else if (strncmp(str, VAL_MIN, strlen(VAL_MIN)) == 0) {
	    int ret = pos_srv_req(mot_id, MOT_CNT_MIN);
            if (ret == 0) updated = true;
        }
    }

    r = cbor_extract_from_map_int(value, VAL_KEY, &int_val);
    if (!r && int_val >= 0) {
        r = pos_srv_req(mot_id, int_val);
        if (r == 0) updated = true;
    }

    if (updated) {
        *rsp_code = COAP_RESPONSE_CODE_CHANGED;
    }

    return r;
}

static int rsrc_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len, int mot_id)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_non_con_setter(sock, resource, request, addr, addr_len,
		    handle_rsrc_post, &mot_id);
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
    int r = 0;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    size_t payload_len;

    r = prepare_rsrc_payload(payload, MAX_COAP_PAYLOAD_LEN, mc_id);
    if (r < 0) {
        return r;
    }
    payload_len = r;

    return coap_server_handle_simple_getter(sock, resource, request, addr, addr_len,
                    payload, payload_len);
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
	  .put = rsrc0_post,
          .path = rsrc0_path,
	},
	{ .get = rsrc1_get,
	  .post = rsrc1_post,
	  .put = rsrc1_post,
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
