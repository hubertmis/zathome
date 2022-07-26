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

#define RED_KEY "r"
#define GREEN_KEY "g"
#define BLUE_KEY "b"
#define WHITE_KEY "w"
#define DUR_KEY "d"
#define PRESET_KEY "p"

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
    unsigned r, g, b, w, dur = 0;
    int new_dur, p;

    if (led_get(&r, &g, &b, &w) != 0) return -EINVAL;

    // Handle red
    ret = handle_color(value, RED_KEY, &r);
    if (!ret) {
        updated = true;
    }

    // Handle green
    ret = handle_color(value, GREEN_KEY, &g);
    if (!ret) {
        updated = true;
    }

    // Handle blue
    ret = handle_color(value, BLUE_KEY, &b);
    if (!ret) {
        updated = true;
    }

    // Handle white
    ret = handle_color(value, WHITE_KEY, &w);
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
        ret = preset_get(p, &r, &g, &b, &w, &dur);
        if (!ret) {
            updated = true;
        }
    }

    if (updated) {
        *rsp_code = COAP_RESPONSE_CODE_CHANGED;
        led_anim(r, g, b, w, dur);
    }

    return ret;
}

static int rgb_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_simple_setter(sock, resource, request, addr, addr_len,
		    handle_rgbw_post, NULL);
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
