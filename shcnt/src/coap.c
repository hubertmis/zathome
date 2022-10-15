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

#include "debug_log.h"

static int prepare_dbg_payload(uint8_t *payload, size_t len)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;
    uint32_t *log;
    uint32_t log_len = debug_log_get(&log);

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_array(&ce, &map, log_len) != CborNoError) return -EINVAL;

    for (int i = 0; i < log_len; i++) {
	    if (cbor_encode_int(&map, log[i]) != CborNoError) return -EINVAL;
    }

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int dbg_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    int r = 0;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN*6];
    size_t payload_len;

    r = prepare_dbg_payload(payload, sizeof(payload));
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

#define REQ_KEY "r"
#define OVR_KEY "o"
#define PRJ_KEY "p"

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

    int req;
    int override;
    bool prj;
    int r = pos_srv_get(id, &req, &override, &prj);

    if (r) return r;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 4) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, VAL_KEY, strlen(VAL_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, value) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, REQ_KEY, strlen(REQ_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, req) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, OVR_KEY, strlen(OVR_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, override) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, PRJ_KEY, strlen(PRJ_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_boolean(&map, prj) != CborNoError) return -EINVAL;

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

#define VALIDITY_KEY "d"
#define PRJ_KEY "p"

static int handle_prj_post(CborValue *value,
	       	enum coap_response_code *rsp_code, void *context)
{
    int mot_id = *(int *)context;
    int ret;
    int validity_ms = 2 * 60 * 1000;
    bool prj_active = false;

    *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;

    // Handle validity
    ret = cbor_extract_from_map_int(value, VALIDITY_KEY, &validity_ms);
    if (validity_ms <= 0) {
        *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
        return -EINVAL;
    }

    // Handle projector being enabled
    ret = cbor_extract_from_map_bool(value, PRJ_KEY, &prj_active);
    if (ret) {
        *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
        return -EINVAL;
    }

    pos_srv_set_projector_state(mot_id, prj_active, validity_ms);

    *rsp_code = COAP_RESPONSE_CODE_CHANGED;
    return 0;
}

static int prj_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len, int mot_id)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_non_con_setter(sock, resource, request, addr, addr_len,
		    handle_prj_post, &mot_id);
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

static int prj0_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
	return prj_post(resource, request, addr, addr_len, 0);
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

static int prj1_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
	return prj_post(resource, request, addr, addr_len, 1);
}

static struct coap_resource * rsrcs_get(int sock)
{
    static const char * const fota_path [] = {"fota_req", NULL};
    static const char * const sd_path [] = {"sd", NULL};
    static const char * const prov_path[] = {"prov", NULL};
    static const char * const dbg_path[] = {"dbg", NULL};
    static const char * rsrc0_path[] = {NULL, NULL};
    static const char * prj0_path[] = {NULL, "prj", NULL};
    static const char * rsrc1_path[] = {NULL, NULL};
    static const char * prj1_path[] = {NULL, "prj", NULL};

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
	{ .get = dbg_get,
	  .path = dbg_path,
	},
	{ .get = rsrc0_get,
	  .post = rsrc0_post,
	  .put = rsrc0_post,
          .path = rsrc0_path,
	},
	{ .post = prj0_post,
	  .path = prj0_path,
	},
	{ .get = rsrc1_get,
	  .post = rsrc1_post,
	  .put = rsrc1_post,
          .path = rsrc1_path,
	},
	{ .post = prj1_post,
	  .path = prj1_path,
	},
        { .path = NULL } // Array terminator
    };

    int rsrc0_index = ARRAY_SIZE(resources) - 5;
    int rsrc1_index = ARRAY_SIZE(resources) - 3;

    rsrc0_path[0] = prov_get_rsrc_label(0);
    prj0_path[0] = rsrc0_path[0];
    rsrc1_path[0] = prov_get_rsrc_label(1);
    prj1_path[0] = rsrc1_path[0];

    if (!rsrc0_path[0] || !strlen(rsrc0_path[0])) {
	    resources[rsrc0_index].path = NULL;
    } else {
	    resources[rsrc0_index].path = rsrc0_path;
    }

    if (!rsrc1_path[0] || !strlen(rsrc1_path[0])) {
	    resources[rsrc1_index].path = NULL;
    } else {
	    resources[rsrc1_index].path = rsrc1_path;
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
