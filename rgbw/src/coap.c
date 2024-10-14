/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <cbor_utils.h>
#include <coap_fota.h>
#include <coap_reboot.h>
#include <coap_sd.h>
#include <coap_server.h>
#include "data_dispatcher.h"
#include "led.h"
#include "led_ctlr.h"
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

#define MANUAL_VALIDITY_MS (10UL * 3600UL * 1000UL)

#define RED_KEY "r"
#define GREEN_KEY "g"
#define BLUE_KEY "b"
#define WHITE_KEY "w"
#define PRESET_KEY "p"

#define RSRC_KEY "r"
#define PRESET_FMT PRESET_KEY "%d"
#define PRESET_MAX_KEY_SIZE (sizeof(PRESET_KEY) + 2)

static int handle_prov_post(CborValue *value, 
	       	enum coap_response_code *rsp_code, void *context)
{
    (void)context;

    bool updated = false;
    int r;
    char str[PROV_LBL_MAX_LEN];

    // Handle rsrc
    r = cbor_extract_from_map_string(value, RSRC_KEY, str, sizeof(str));
    if ((r >= 0) && (r < PROV_LBL_MAX_LEN)) {
        r = prov_set_rsrc_label(str);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle preset
    for (int i = 0; i < PROV_NUM_PRESETS; i++) {
        CborValue map_value;
        CborError cbor_error;
        char key[PRESET_MAX_KEY_SIZE];
        int key_len;

        struct prov_leds_brightness leds_value;

        key_len = snprintf(key, sizeof(key), PRESET_FMT, i);
        if (key_len < 0 || key_len >= sizeof(key)) {
            continue;
        }

        cbor_error = cbor_value_map_find_value(value, key, &map_value);
        if ((cbor_error == CborNoError) && cbor_value_is_map(&map_value)) {
            // TODO: extract r, g, b, w
            r = cbor_extract_from_map_int(&map_value, RED_KEY, &leds_value.r);
            if (r != 0) continue;
            r = cbor_extract_from_map_int(&map_value, GREEN_KEY, &leds_value.g);
            if (r != 0) continue;
            r = cbor_extract_from_map_int(&map_value, BLUE_KEY, &leds_value.b);
            if (r != 0) continue;
            r = cbor_extract_from_map_int(&map_value, WHITE_KEY, &leds_value.w);
            if (r != 0) continue;

            r = prov_set_preset(i, &leds_value);

            if (r == 0) {
                updated = true;
            }
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

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, CborIndefiniteLength) != CborNoError) return -EINVAL;

    label = prov_get_rsrc_label();
    if (cbor_encode_text_string(&map, RSRC_KEY, strlen(RSRC_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&map, label, strlen(label)) != CborNoError) return -EINVAL;

    // Handle presets
    for (int i = 0; i < PROV_NUM_PRESETS; i++) {
        CborEncoder preset_map;
        char key[PRESET_MAX_KEY_SIZE];
        int key_len;
        int r;

        struct prov_leds_brightness value;
        r = prov_get_preset(i, &value);

        if (r < 0) continue;

        key_len = snprintf(key, sizeof(key), PRESET_FMT, i);
        if (key_len < 0 || key_len >= sizeof(key)) {
            continue;
        }

        if (cbor_encode_text_string(&map, key, key_len) != CborNoError) return -EINVAL;
        if (cbor_encoder_create_map(&map, &preset_map, 4) != CborNoError) return -EINVAL;

        if (cbor_encode_text_string(&preset_map, RED_KEY, strlen(RED_KEY)) != CborNoError) return -EINVAL;
        if (cbor_encode_uint(&preset_map, value.r) != CborNoError) return -EINVAL;

        if (cbor_encode_text_string(&preset_map, GREEN_KEY, strlen(GREEN_KEY)) != CborNoError) return -EINVAL;
        if (cbor_encode_uint(&preset_map, value.g) != CborNoError) return -EINVAL;

        if (cbor_encode_text_string(&preset_map, BLUE_KEY, strlen(BLUE_KEY)) != CborNoError) return -EINVAL;
        if (cbor_encode_uint(&preset_map, value.b) != CborNoError) return -EINVAL;

        if (cbor_encode_text_string(&preset_map, WHITE_KEY, strlen(WHITE_KEY)) != CborNoError) return -EINVAL;
        if (cbor_encode_uint(&preset_map, value.w) != CborNoError) return -EINVAL;

        if (cbor_encoder_close_container(&map, &preset_map) != CborNoError) return -EINVAL;
    }

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

    r = coap_server_handle_simple_getter(sock, resource, request, addr, addr_len,
                    payload, payload_len);

    return r;
}

#define DUR_KEY "d"
#define RESET_KEY "res"

static int handle_color(CborValue *value, const char *key, unsigned *color_val)
{
    int new_color;
    int ret = cbor_extract_from_map_int(value, key, &new_color);

    if (!ret) {
        if ((new_color <= MAX_BRIGHTNESS) && (new_color >= 0)) {
            *color_val = new_color;
	    return 0;
	} else {
	    return -EINVAL;
	}
    }

    return ret;
}

static int handle_rgbw_post(CborValue *value, enum coap_response_code *rsp_code, void *context)
{
    bool updated = false;
    int ret;
    leds_brightness leds;
    unsigned dur = 0;
    int new_dur, p;
    bool reset = false;

    if (led_get(&leds) != 0) {
        *rsp_code = COAP_RESPONSE_CODE_INTERNAL_ERROR;
        return -EINVAL;
    }

    *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;

    // Handle red
    ret = handle_color(value, RED_KEY, &leds.r);
    if (!ret) {
        updated = true;
    }

    // Handle green
    ret = handle_color(value, GREEN_KEY, &leds.g);
    if (!ret) {
        updated = true;
    }

    // Handle blue
    ret = handle_color(value, BLUE_KEY, &leds.b);
    if (!ret) {
        updated = true;
    }

    // Handle white
    ret = handle_color(value, WHITE_KEY, &leds.w);
    if (!ret) {
        updated = true;
    }

    // Handle duration
    ret = cbor_extract_from_map_int(value, DUR_KEY, &new_dur);
    if (!ret) {
        dur = new_dur;
    }

    // Handle preset
    ret = cbor_extract_from_map_int(value, PRESET_KEY, &p);
    if (!ret) {
        ret = preset_get(p, &leds, &dur);
        if (!ret) {
            updated = true;
        }
    }

    // Handle reset
    ret = cbor_extract_from_map_bool(value, RESET_KEY, &reset);
    if (!ret && reset) {
        *rsp_code = COAP_RESPONSE_CODE_CHANGED;
	led_ctlr_reset_manual();
	updated = false;
    }

    if (updated) {
        *rsp_code = COAP_RESPONSE_CODE_CHANGED;
        led_ctlr_set_manual(&leds, dur, MANUAL_VALIDITY_MS);
    }

    return ret;
}

static int rgb_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_non_con_setter(sock, resource, request, addr, addr_len,
		    handle_rgbw_post, NULL);
}


static int prepare_rgb_payload(uint8_t *payload, size_t len)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;
    leds_brightness leds;

    if (led_get(&leds) != 0) return -EINVAL;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 4) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, RED_KEY, strlen(RED_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_uint(&map, leds.r) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, GREEN_KEY, strlen(GREEN_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_uint(&map, leds.g) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, BLUE_KEY, strlen(BLUE_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_uint(&map, leds.b) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, WHITE_KEY, strlen(WHITE_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_uint(&map, leds.w) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int rgb_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    int r = 0;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    size_t payload_len;

    r = prepare_rgb_payload(payload, MAX_COAP_PAYLOAD_LEN);
    if (r < 0) {
        return r;
    }
    payload_len = r;

    return coap_server_handle_simple_getter(sock, resource, request, addr, addr_len,
                    payload, payload_len);
}

static int handle_auto_post(CborValue *value, enum coap_response_code *rsp_code, void *context)
{
    bool r_valid = false;
    bool g_valid = false;
    bool b_valid = false;
    bool w_valid = false;
    int ret;
    leds_brightness leds;

    // Handle red
    ret = handle_color(value, RED_KEY, &leds.r);
    if (!ret) {
        r_valid = true;
    }

    // Handle green
    ret = handle_color(value, GREEN_KEY, &leds.g);
    if (!ret) {
        g_valid = true;
    }

    // Handle blue
    ret = handle_color(value, BLUE_KEY, &leds.b);
    if (!ret) {
        b_valid = true;
    }

    // Handle white
    ret = handle_color(value, WHITE_KEY, &leds.w);
    if (!ret) {
        w_valid = true;
    }

    if (r_valid && g_valid && b_valid && w_valid) {
        *rsp_code = COAP_RESPONSE_CODE_CHANGED;
        led_ctlr_set_auto(&leds);
    } else {
        *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
    }

    return ret;
}

static int auto_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_simple_setter(sock, resource, request, addr, addr_len,
		    handle_auto_post, NULL);
}

#if 0
static int handle_dim_post(CborValue *value, enum coap_response_code *rsp_code, void *context)
{
    int ret;
    int duration_ms = 0;
    bool duration_valid = false;

    // Handle duration
    ret = cbor_extract_from_map_int(value, DUR_KEY, &duration_ms);
    if (!ret) {
	    duration_valid = true;
    }

    if (duration_valid && (duration_ms >= 0)) {
        *rsp_code = COAP_RESPONSE_CODE_CHANGED;

        if (duration_ms > 0) {
            led_ctlr_dim(duration_ms);
        } else {
            led_ctlr_reset_dimmer();
	}
    } else {
        *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
    }

    return ret;
}

static int dim_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_simple_setter(sock, resource, request, addr, addr_len,
		    handle_dim_post, NULL);
}
#endif

#define PRJ_KEY "p"

static int handle_prj_post(CborValue *value, enum coap_response_code *rsp_code, void *context)
{
    int ret;
    int duration_ms = 2 * 60 * 1000;
    bool prj_active = false;

    // Handle duration
    ret = cbor_extract_from_map_int(value, DUR_KEY, &duration_ms);
    if (duration_ms <= 0) {
        *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
        return -EINVAL;
    }

    // Handle projector being enabled
    ret = cbor_extract_from_map_bool(value, PRJ_KEY, &prj_active);
    if (ret) {
        *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
        return -EINVAL;
    }

    if (prj_active) {
        led_ctlr_dim(duration_ms);
    } else {
        led_ctlr_reset_dimmer();
    }

    *rsp_code = COAP_RESPONSE_CODE_CHANGED;
    return 0;
}

static int prj_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_non_con_setter(sock, resource, request, addr, addr_len,
		    handle_prj_post, NULL);
}

static struct coap_resource * rsrcs_get(int sock)
{
    static const char * const fota_path [] = {"fota_req", NULL};
    static const char * const sd_path [] = {"sd", NULL};
    static const char * const prov_path[] = {"prov", NULL};
    static const char * const reboot_path[] = {"reboot", NULL};
    static const char * const rgb_path[] = {"rgb", NULL};
    static const char * rsrc_path[] = {NULL, NULL};
    static const char * auto_path[] = {NULL, "auto", NULL};
    static const char * prj_path[] = {NULL, "prj", NULL};

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
	{ .get = rgb_get,
	  .post = rgb_post,
	  .path = rgb_path,
	},
	{ .get = rgb_get,
	  .post = rgb_post,
          .path = rsrc_path,
	},
	{ .post = auto_post,
	  .path = auto_path,
	},
	{ .post = prj_post,
	  .path = prj_path,
	},
        { .path = NULL } // Array terminator
    };

    rsrc_path[0] = prov_get_rsrc_label();
    auto_path[0] = rsrc_path[0];
    prj_path[0] = rsrc_path[0];

    if (!rsrc_path[0] || !strlen(rsrc_path[0])) {
	    resources[ARRAY_SIZE(resources)-4].path = NULL;
    } else {
	    resources[ARRAY_SIZE(resources)-4].path = rsrc_path;
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
