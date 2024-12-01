/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap.h"

#include <errno.h>
#include <stdint.h>

#include <cbor_utils.h>
#include <coap_fota.h>
#include <coap_reboot.h>
#include <coap_sd.h>
#include <coap_server.h>
#include <continuous_sd.h>
#include "data_dispatcher.h"
#include "prov.h"

#include <net/fota_download.h>
#include <zephyr/kernel.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/random/random.h>

#define COAP_PORT 5683
#define COAPS_PORT 5684
#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 128

#define MAX_FOTA_PAYLOAD_LEN 64
#define MAX_FOTA_PATH_LEN 16

#define COAP_CONTENT_FORMAT_TEXT 0
#define COAP_CONTENT_FORMAT_CBOR 60

#ifndef COAPS_PSK
#error PSK for coaps is not defined
#endif
#define COAPS_PSK_ID "def"

#define SITE_LOCAL_SCOPE 5

// Do not block global access until SO_PROTOCOL and verification of ULA address are available downstream
#define BLOCK_GLOBAL_ACCESS 0
#if BLOCK_GLOBAL_ACCESS
#define SO_PROTOCOL 38

static inline bool net_ipv6_is_ula_addr(const struct in6_addr *addr)
{
    return (addr->s6_addr[0] & 0xFE) == 0xFC;
}
#endif

#if BLOCK_GLOBAL_ACCESS
static bool addr_is_local(const struct sockaddr *addr, socklen_t addr_len)
{
    const struct sockaddr_in6 *addr6;
    const struct in6_addr *in6_addr;

    if (addr->sa_family != AF_INET6) {
        return false;
    }

    addr6 = (struct sockaddr_in6 *)addr;
    in6_addr = &addr6->sin6_addr;

    if (net_ipv6_is_ula_addr(in6_addr) ||
            net_ipv6_is_ll_addr(in6_addr)) {
        return true;
    }

    for (int i = 1; i <= SITE_LOCAL_SCOPE; ++i) {
        if (net_ipv6_is_addr_mcast_scope(in6_addr, i)) {
            return true;
        }
    }

    return false;
}

static bool sock_is_secure(int sock)
{
    int proto;
    socklen_t protolen = sizeof(proto);
    int r;

    r = getsockopt(sock, SOL_SOCKET, SO_PROTOCOL, &proto, &protolen);

    if ((r < 0) || (protolen != sizeof(proto))) {
        return false;
    }

    return proto == IPPROTO_DTLS_1_2;
}
#endif


#define MEAS_KEY "m"
#define SETT_KEY "s"
#define OUT_KEY  "o"
#define CNT_KEY  "c"
#define P_KEY    "p"
#define I_KEY    "i"
#define HYST_KEY "h"

#define TAG_DECIMAL_FRACTION 4

static const char * cnt_val_map[] = {
    [DATA_CTLR_ONOFF] = "onoff",
    [DATA_CTLR_PID]   = "pid",
};

static int prepare_temp_payload(uint8_t *payload, size_t len, data_loc_t loc)
{
    const data_dispatcher_publish_t *meas, *sett, *output, *ctlr;
    ZCBOR_STATE_E(ce, 3, payload, len, 1);
    data_ctlr_mode_t ctlr_mode;
    const char *cm_str;

    data_dispatcher_get(DATA_TEMP_MEASUREMENT, loc, &meas);
    data_dispatcher_get(DATA_TEMP_SETTING, loc, &sett);
    data_dispatcher_get(DATA_OUTPUT, loc, &output);
    data_dispatcher_get(DATA_CONTROLLER, loc, &ctlr);

    if (!zcbor_map_start_encode(ce, 4)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, MEAS_KEY)) return -EINVAL;
    if (cbor_encode_dec_frac_num(ce, -1, meas->temp_measurement)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, SETT_KEY)) return -EINVAL;
    if (cbor_encode_dec_frac_num(ce, -1, sett->temp_setting)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, OUT_KEY)) return -EINVAL;
    if (!zcbor_int32_put(ce, output->output)) return -EINVAL;

    ctlr_mode = ctlr->controller.mode;
    cm_str = cnt_val_map[ctlr_mode];
    uint32_t num_ctlr_map_items = ctlr_mode == DATA_CTLR_ONOFF ? 2 : 3;
    if (!zcbor_tstr_put_lit(ce, CNT_KEY)) return -EINVAL;
    if (!zcbor_map_start_encode(ce, num_ctlr_map_items)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, CNT_KEY)) return -EINVAL;
    if (!zcbor_tstr_put_term(ce, cm_str, 6)) return -EINVAL;

    if (ctlr_mode == DATA_CTLR_ONOFF) {
        if (!zcbor_tstr_put_lit(ce, HYST_KEY)) return -EINVAL;
        if (!zcbor_int32_put(ce, ctlr->controller.hysteresis)) return -EINVAL;
    } else {
        if (!zcbor_tstr_put_lit(ce, P_KEY)) return -EINVAL;
        if (!zcbor_int32_put(ce, ctlr->controller.p)) return -EINVAL;

        if (!zcbor_tstr_put_lit(ce, I_KEY)) return -EINVAL;
        if (!zcbor_int32_put(ce, ctlr->controller.i)) return -EINVAL;
    }

    if (!zcbor_map_end_encode(ce, num_ctlr_map_items)) return -EINVAL;
    if (!zcbor_map_end_encode(ce, 4)) return -EINVAL;

    return (size_t)(ce->payload - payload);
}

static int temp_handler(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len,
             data_loc_t loc)
{
    int sock = *(int*)resource->user_data;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    uint16_t payload_len;
    int r = prepare_temp_payload(payload, MAX_COAP_PAYLOAD_LEN, loc);
    if (r < 0) {
        return -r;
    }
    payload_len = r;

    return coap_server_handle_simple_getter(sock, resource, request,
                    addr, addr_len, payload, payload_len);
}


static int handle_temp_post(zcbor_state_t *cd, enum coap_response_code *rsp_code, void *context)
{
    int r = 0;

    *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
    data_loc_t *loc = context;

    // Handle temperature setting
    if (zcbor_search_key_tstr_lit(cd, SETT_KEY)) {
        int temp_val;
        if (cbor_decode_dec_frac_num(cd, -1, &temp_val)) {
            *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
            return -EINVAL;
        }

        data_dispatcher_publish_t sett = {
            .loc = *loc,
            .type = DATA_TEMP_SETTING,
            .temp_setting = temp_val,
        };
        data_dispatcher_publish(&sett);

        *rsp_code = COAP_RESPONSE_CODE_CHANGED;
    }

    // Handle controller
    if (zcbor_search_key_tstr_lit(cd, CNT_KEY)) {
        if (!zcbor_unordered_map_start_decode(cd)) {
            *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
            return -EINVAL;
        }

        bool updated = false;
        const data_dispatcher_publish_t *ctlr;
        data_dispatcher_publish_t new_ctlr;
        data_dispatcher_get(DATA_CONTROLLER, *loc, &ctlr);
        new_ctlr = *ctlr;

        char str[6];
	int int_val;

        // Handle controller mode
	r = cbor_extract_from_map_string(cd, CNT_KEY, str, sizeof(str));
	if (r >= 0) {
           for (size_t i = 0; i < sizeof(cnt_val_map) / sizeof(cnt_val_map[0]); ++i) {
               if (strncmp(cnt_val_map[i], str, sizeof(str)) == 0) {
                   new_ctlr.controller.mode = i;
                   updated = true;
                   break;
               }
           }
	}

        // Handle hysteresis
	r = cbor_extract_from_map_int(cd, HYST_KEY, &int_val);
        if (r == 0) {
            new_ctlr.controller.hysteresis = int_val;
            updated = true;
        }

        // Handle P
	r = cbor_extract_from_map_int(cd, P_KEY, &int_val);
        if (r == 0) {
            new_ctlr.controller.p = int_val;
            updated = true;
        }

        // Handle I
	r = cbor_extract_from_map_int(cd, I_KEY, &int_val);
        if (r == 0) {
            new_ctlr.controller.i = int_val;
            updated = true;
        }

        if (updated) {
            *rsp_code = COAP_RESPONSE_CODE_CHANGED;
            data_dispatcher_publish(&new_ctlr);
        }
    }

    return r;
}

static int temp_post(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len,
             data_loc_t loc)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_simple_setter(sock, resource, request, addr, addr_len, handle_temp_post, &loc);
}

static int temp_local_get(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    return temp_handler(resource, request, addr, addr_len, DATA_LOC_LOCAL);
}

static int temp_local_post(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    return temp_post(resource, request, addr, addr_len, DATA_LOC_LOCAL);
}

static int temp_remote_get(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    return temp_handler(resource, request, addr, addr_len, DATA_LOC_REMOTE);
}

static int temp_remote_post(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    return temp_post(resource, request, addr, addr_len, DATA_LOC_REMOTE);
}

#define RSRC0_KEY "r0"
#define RSRC1_KEY "r1"
#define OUT0_KEY "o0"

static int handle_prov_post(zcbor_state_t *cd, enum coap_response_code *rsp_code, void *context)
{
    (void)context;
    int r = -EINVAL;
    bool updated = false;
    char str[PROV_LBL_MAX_LEN];

    // Handle rsrc0
    r = cbor_extract_from_map_string(cd, RSRC0_KEY, str, sizeof(str));
    if ((r >= 0) && (r < PROV_LBL_MAX_LEN)) {
        r = prov_set_rsrc_label(DATA_LOC_LOCAL, str);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle rsrc1
    r = cbor_extract_from_map_string(cd, RSRC1_KEY, str, sizeof(str));
    if ((r >= 0) && (r < PROV_LBL_MAX_LEN)) {
        r = prov_set_rsrc_label(DATA_LOC_REMOTE, str);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle out0
    r = cbor_extract_from_map_string(cd, OUT0_KEY, str, sizeof(str));
    if ((r >= 0) && (r < PROV_LBL_MAX_LEN)) {
        r = prov_set_loc_output_label(str);

        if (r == 0) {
            updated = true;
        }
    }

    if (updated) {
        *rsp_code = COAP_RESPONSE_CODE_CHANGED;
        prov_store();
    } else {
        *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
    }

    return r;
}

static int prov_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_simple_setter(sock, resource, request ,addr, addr_len,
		    handle_prov_post, NULL);
}

static int prepare_prov_payload(uint8_t *payload, size_t len)
{
    ZCBOR_STATE_E(ce, 2, payload, len, 1);
    const char *label;

    if (!zcbor_map_start_encode(ce, 3)) return -EINVAL;

    label = prov_get_rsrc_label(DATA_LOC_LOCAL);
    if (!zcbor_tstr_put_lit(ce, RSRC0_KEY)) return -EINVAL;
    if (!zcbor_tstr_put_term(ce, label, 8)) return -EINVAL;

    label = prov_get_rsrc_label(DATA_LOC_REMOTE);
    if (!zcbor_tstr_put_lit(ce, RSRC1_KEY)) return -EINVAL;
    if (!zcbor_tstr_put_term(ce, label, 8)) return -EINVAL;

    label = prov_get_loc_output_label();
    if (!zcbor_tstr_put_lit(ce, OUT0_KEY)) return -EINVAL;
    if (!zcbor_tstr_put_term(ce, label, 8)) return -EINVAL;

    if (!zcbor_map_end_encode(ce, 3)) return -EINVAL;

    return (size_t)(ce->payload - payload);
}

static int prov_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    int r = 0;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    size_t payload_len = 0;

    r = prepare_prov_payload(payload, MAX_COAP_PAYLOAD_LEN);
    if (r < 0) {
        return r;
    }
    payload_len = r;

    return coap_server_handle_simple_getter(sock, resource, request,
                    addr, addr_len, payload, payload_len);
}

static int prepare_cont_sd_dbg_payload(uint8_t *payload, size_t len)
{
    ZCBOR_STATE_E(ce, 2, payload, len, 1);

    int state;
    int64_t target_time;
    int64_t now = k_uptime_get();
    const char *name;
    const char *type;
    int sd_missed;
    int64_t last_req;
    int64_t last_rsp;
    int sem_take_result;
    k_ticks_t thread_rem_ticks;
    continuous_sd_debug(&state, &target_time, &name, &type, &sd_missed, &last_req, &last_rsp,
			&sem_take_result, &thread_rem_ticks);


    if (!zcbor_map_start_encode(ce, 10)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, "n")) return -EINVAL;
    if (!zcbor_tstr_put_term(ce, name, 8)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, "t")) return -EINVAL;
    if (!zcbor_tstr_put_term(ce, type, 8)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, "s")) return -EINVAL;
    if (!zcbor_int32_put(ce, state)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, "nt")) return -EINVAL;
    if (!zcbor_int64_put(ce, now)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, "tt")) return -EINVAL;
    if (!zcbor_int64_put(ce, target_time)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, "sm")) return -EINVAL;
    if (!zcbor_int32_put(ce, sd_missed)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, "lre")) return -EINVAL;
    if (!zcbor_int64_put(ce, last_req)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, "lrs")) return -EINVAL;
    if (!zcbor_int64_put(ce, last_rsp)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, "str")) return -EINVAL;
    if (!zcbor_int32_put(ce, sem_take_result)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, "trt")) return -EINVAL;
    if (!zcbor_int64_put(ce, thread_rem_ticks)) return -EINVAL;

    if (!zcbor_map_end_encode(ce, 10)) return -EINVAL;

    return (size_t)(ce->payload - payload);
}

static int cont_sd_dbg_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    size_t payload_len = 0;

    int r = prepare_cont_sd_dbg_payload(payload, MAX_COAP_PAYLOAD_LEN);
    if (r < 0) {
        return r;
    }
    payload_len = r;

    return coap_server_handle_simple_getter(sock, resource, request,
                    addr, addr_len, payload, payload_len);
}

#define VALIDITY_KEY "d"
#define PRJ_KEY "p"

static int handle_prj_post(zcbor_state_t *value,
	       	enum coap_response_code *rsp_code, void *context)
{
    data_loc_t *loc = context;
    data_loc_t rsrc_id = *loc;
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

    data_dispatcher_publish_t out_data = {
        .type         = DATA_PRJ_ENABLED,
        .loc          = rsrc_id,
        .prj_validity = prj_active ? validity_ms : 0,
    };

    data_dispatcher_publish(&out_data);

    *rsp_code = COAP_RESPONSE_CODE_CHANGED;
    return 0;
}

static int prj_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len, data_loc_t rsrc_id)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_non_con_setter(sock, resource, request, addr, addr_len,
		    handle_prj_post, &rsrc_id);
}

static int prj_remote_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
	return prj_post(resource, request, addr, addr_len, DATA_LOC_REMOTE);
}

static int prj_local_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
	return prj_post(resource, request, addr, addr_len, DATA_LOC_LOCAL);
}

static int prepare_prj_payload(uint8_t *payload, size_t len, data_loc_t loc)
{
    ZCBOR_STATE_E(ce, 2, payload, len, 1);
    const data_dispatcher_publish_t *prj;

    data_dispatcher_get(DATA_PRJ_ENABLED, loc, &prj);
    uint16_t prj_validity = prj->prj_validity;

    size_t map_len = prj_validity ? 2 : 1;

    if (!zcbor_map_start_encode(ce, map_len)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, PRJ_KEY)) return -EINVAL;
    if (!zcbor_bool_put(ce, prj_validity > 0)) return -EINVAL;

    if (prj_validity) {
        if (!zcbor_tstr_put_lit(ce, VALIDITY_KEY)) return -EINVAL;
	if (!zcbor_uint32_put(ce, prj_validity)) return -EINVAL;
    }

    if (!zcbor_map_end_encode(ce, map_len)) return -EINVAL;

    return (size_t)(ce->payload - payload);
}

static int prj_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len,
	data_loc_t loc)
{
    int sock = *(int*)resource->user_data;
    int r = 0;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    size_t payload_len = 0;

    r = prepare_prj_payload(payload, MAX_COAP_PAYLOAD_LEN, loc);
    if (r < 0) {
        return r;
    }
    payload_len = r;

    return coap_server_handle_simple_getter(sock, resource, request,
                    addr, addr_len, payload, payload_len);
}

static int prj_remote_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
	return prj_get(resource, request, addr, addr_len, DATA_LOC_REMOTE);
}
static int prj_local_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
	return prj_get(resource, request, addr, addr_len, DATA_LOC_LOCAL);
}

static struct coap_resource * rsrcs_get(int sock)
{
    static const char * const fota_path [] = {"fota_req", NULL};
    static const char * const sd_path [] = {"sd", NULL};
    static const char * const prov_path[] = {"prov", NULL};
    static const char * const reboot_path[] = {"reboot", NULL};
    static const char * const cont_sd_dbg_path[] = {"cont_sd", NULL};
    static const char * rsrc_remote_path[] = {NULL, NULL};
    static const char * prj_remote_path[] = {NULL, "prj", NULL};
    static const char * rsrc_local_path[] = {NULL, NULL};
    static const char * prj_local_path[] = {NULL, "prj", NULL};

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
	{ .post = coap_reboot_post,
      .path = reboot_path,
	},
	{ .get = cont_sd_dbg_get,
	  .path = cont_sd_dbg_path,
	},
	{ .get = temp_remote_get,
	  .post = temp_remote_post,
          .path = rsrc_remote_path,
	},
	{ .post = prj_remote_post,
	  .get = prj_remote_get,
	  .path = prj_remote_path,
	},
	{ .get = temp_local_get,
	  .post = temp_local_post,
          .path = rsrc_local_path,
	},
	{ .post = prj_local_post,
	  .get = prj_local_get,
	  .path = prj_local_path,
	},
        { .path = NULL } // Array terminator
    };

    const int rsrc_local_index = ARRAY_SIZE(resources) - 3;

    rsrc_remote_path[0] = prov_get_rsrc_label(DATA_LOC_REMOTE);
    prj_remote_path[0] = rsrc_remote_path[0];
    rsrc_local_path[0] = prov_get_rsrc_label(DATA_LOC_LOCAL);
    prj_local_path[0] = rsrc_local_path[0];

    if (!rsrc_local_path[0] || !strlen(rsrc_local_path[0])) {
	    resources[rsrc_local_index].path = NULL;
    } else {
	    resources[rsrc_local_index].path = rsrc_local_path;
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
