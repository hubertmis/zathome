/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap.h"

#include <coap_fota.h>
#include <coap_reboot.h>
#include <coap_sd.h>
#include <coap_server.h>
#include "led.h"
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
#define OUT0_KEY "o0"
#define OUT1_KEY "o1"
#define ANALOG0_KEY "a0"
#define ANALOG1_KEY "a1"
#define THRESHOLD0_KEY "t0"
#define THRESHOLD1_KEY "t1"
#define MONOSTABLE_KEY "m"

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

    // Handle output0
    cbor_error = cbor_value_map_find_value(&value, OUT0_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_text_string(&map_val)) {
        size_t str_len;

        cbor_error = cbor_value_get_string_length(&map_val, &str_len);
        if ((cbor_error == CborNoError) && (str_len < PROV_LBL_MAX_LEN)) {
            char str[PROV_LBL_MAX_LEN];
            str_len = PROV_LBL_MAX_LEN;

            cbor_error = cbor_value_copy_text_string(&map_val, str, &str_len, NULL);
            if (cbor_error == CborNoError) {
                r = prov_set_output_rsrc_label(0, str);

                if (r == 0) {
                    updated = true;
                }
            }
        }
    }

    // Handle output1
    cbor_error = cbor_value_map_find_value(&value, OUT1_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_text_string(&map_val)) {
        size_t str_len;

        cbor_error = cbor_value_get_string_length(&map_val, &str_len);
        if ((cbor_error == CborNoError) && (str_len < PROV_LBL_MAX_LEN)) {
            char str[PROV_LBL_MAX_LEN];
            str_len = PROV_LBL_MAX_LEN;

            cbor_error = cbor_value_copy_text_string(&map_val, str, &str_len, NULL);
            if (cbor_error == CborNoError) {
                r = prov_set_output_rsrc_label(1, str);

                if (r == 0) {
                    updated = true;
                }
            }
        }
    }

    // Handle analog0
    cbor_error = cbor_value_map_find_value(&value, ANALOG0_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_boolean(&map_val)) {
        bool enabled;
        cbor_error = cbor_value_get_boolean(&map_val, &enabled);
        if (cbor_error == CborNoError) {
            r = prov_set_analog_enabled(0, enabled);

            if (r == 0) {
                updated = true;
            }
        }
    }

    // Handle analog1
    cbor_error = cbor_value_map_find_value(&value, ANALOG1_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_boolean(&map_val)) {
        bool enabled;
        cbor_error = cbor_value_get_boolean(&map_val, &enabled);
        if (cbor_error == CborNoError) {
            r = prov_set_analog_enabled(1, enabled);

            if (r == 0) {
                updated = true;
            }
        }
    }

    // Handle threshold0
    cbor_error = cbor_value_map_find_value(&value, THRESHOLD0_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_unsigned_integer(&map_val)) {
        uint64_t req_threshold;

        cbor_error = cbor_value_get_uint64(&map_val, &req_threshold);
        if ((cbor_error == CborNoError) && (req_threshold <= UINT16_MAX)) {
            r = prov_set_analog_threshold(0, req_threshold);

            if (r == 0) {
                updated = true;
            }
        }
    }

    // Handle threshold1
    cbor_error = cbor_value_map_find_value(&value, THRESHOLD1_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_unsigned_integer(&map_val)) {
        uint64_t req_threshold;

        cbor_error = cbor_value_get_uint64(&map_val, &req_threshold);
        if ((cbor_error == CborNoError) && (req_threshold <= UINT16_MAX)) {
            r = prov_set_analog_threshold(1, req_threshold);

            if (r == 0) {
                updated = true;
            }
        }
    }

    // Handle monostable
    cbor_error = cbor_value_map_find_value(&value, MONOSTABLE_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_boolean(&map_val)) {
        bool enabled;
        cbor_error = cbor_value_get_boolean(&map_val, &enabled);
        if (cbor_error == CborNoError) {
            r = prov_set_monostable(enabled);

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
    bool enabled;
    int threshold;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 9) != CborNoError) return -EINVAL;

    label = prov_get_rsrc_label(0);
    if (cbor_encode_text_string(&map, RSRC0_KEY, strlen(RSRC0_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&map, label, strlen(label)) != CborNoError) return -EINVAL;

    label = prov_get_rsrc_label(1);
    if (cbor_encode_text_string(&map, RSRC1_KEY, strlen(RSRC1_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&map, label, strlen(label)) != CborNoError) return -EINVAL;

    label = prov_get_output_rsrc_label(0);
    if (cbor_encode_text_string(&map, OUT0_KEY, strlen(OUT0_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&map, label, strlen(label)) != CborNoError) return -EINVAL;

    label = prov_get_output_rsrc_label(1);
    if (cbor_encode_text_string(&map, OUT1_KEY, strlen(OUT1_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&map, label, strlen(label)) != CborNoError) return -EINVAL;

    enabled = prov_get_analog_enabled(0);
    if (cbor_encode_text_string(&map, ANALOG0_KEY, strlen(ANALOG0_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_boolean(&map, enabled) != CborNoError) return -EINVAL;

    enabled = prov_get_analog_enabled(1);
    if (cbor_encode_text_string(&map, ANALOG1_KEY, strlen(ANALOG1_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_boolean(&map, enabled) != CborNoError) return -EINVAL;

    threshold = prov_get_analog_threshold(0);
    if (cbor_encode_text_string(&map, THRESHOLD0_KEY, strlen(THRESHOLD0_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, threshold) != CborNoError) return -EINVAL;

    threshold = prov_get_analog_threshold(1);
    if (cbor_encode_text_string(&map, THRESHOLD1_KEY, strlen(THRESHOLD1_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, threshold) != CborNoError) return -EINVAL;

    enabled = prov_get_monostable();
    if (cbor_encode_text_string(&map, MONOSTABLE_KEY, strlen(MONOSTABLE_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_boolean(&map, enabled) != CborNoError) return -EINVAL;

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

#define PULSE_KEY "p"

static int pulse_post(struct coap_resource *resource,
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

    // Handle pulse
    cbor_error = cbor_value_map_find_value(&value, PULSE_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_unsigned_integer(&map_val)) {
        uint64_t req_num_pulses;

        cbor_error = cbor_value_get_uint64(&map_val, &req_num_pulses);
        if ((cbor_error == CborNoError) && (req_num_pulses <= ULONG_MAX)) {
            led_set_pulses(req_num_pulses);
            rsp_code = COAP_RESPONSE_CODE_CHANGED;
        }
    }

    r = coap_server_send_ack(sock, addr, addr_len, id, rsp_code, token, tkl);
    return r;
}

#include "analog_switch.h"
#define DEV_KEY "d"
#define ADC_KEY "a"
#define RET_KEY "r"
#define EV_KEY "e"
#define ITER_KEY "i"
#define THRES_KEY "t"
#define DEBOUNCE_KEY "deb"

static const struct device *map_id_to_dev(uint8_t id)
{
	switch (id) {
#if DT_NODE_EXISTS(DT_NODELABEL(as1))
		case 0:
			return DEVICE_DT_GET(DT_NODELABEL(as1));
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(as2))
		case 1:
			return DEVICE_DT_GET(DT_NODELABEL(as2));
#endif
		default:
			return NULL;
	}
}

typedef int (*dev_callback)(const struct device *dev, CborValue *value);

static int call_function_for_devs(CborValue *value, dev_callback callback)
{
    CborError cbor_error;
    CborValue map_val;
    const struct device *dev;
    int r = -EINVAL;

    // Handle device
    cbor_error = cbor_value_map_find_value(value, "d", &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_unsigned_integer(&map_val)) {
        uint64_t dev_id;
        cbor_error = cbor_value_get_uint64(&map_val, &dev_id);
        if ((cbor_error == CborNoError) && (dev_id <= UINT8_MAX)) {
            dev = map_id_to_dev(dev_id);
            if (dev != NULL) {
                r = callback(dev, value);
            }
	}
    } else {
        for (int i = 0; i < 2; ++i) {
            dev = map_id_to_dev(i);
            if (dev != NULL) {
                r = callback(dev, value);
		if (r) return r;
	    } else {
                return -EINVAL;
	    }
	}
    }

    return r;
}


static int prepare_adc_payload(uint8_t *payload, size_t len)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 2) != CborNoError) return -EINVAL;

    for (int i = 0; i < 2; i++) {
	    CborEncoder submap;
	    const struct device *dev = map_id_to_dev(i);
	    if (dev == NULL) return 0;

	    int r = 0;
	    uint16_t val = 0;
	    const struct analog_switch_driver_api *api = dev->api;
	    r = api->get(dev, &val);

	    if (cbor_encode_int(&map, i) != CborNoError) return -EINVAL;
	    if (cbor_encoder_create_map(&map, &submap, 2) != CborNoError) return -EINVAL;

	    if (cbor_encode_text_string(&submap, RET_KEY, strlen(RET_KEY)) != CborNoError) return -EINVAL;
	    if (cbor_encode_int(&submap, r) != CborNoError) return -EINVAL;

	    if (cbor_encode_text_string(&submap, ADC_KEY, strlen(ADC_KEY)) != CborNoError) return -EINVAL;
	    if (cbor_encode_int(&submap, val) != CborNoError) return -EINVAL;

	    if (cbor_encoder_close_container(&ce, &submap) != CborNoError) return -EINVAL;
    }

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int adc_get(struct coap_resource *resource,
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

    r = prepare_adc_payload(payload, MAX_COAP_PAYLOAD_LEN);
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

static int prepare_adc_avg_payload(uint8_t *payload, size_t len)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 2) != CborNoError) return -EINVAL;

    for (int i = 0; i < 2; i++) {
	    CborEncoder submap;
	    const struct device *dev = map_id_to_dev(i);
	    if (dev == NULL) return 0;

	    uint16_t val = 0;
	    uint16_t ev = 0;
	    const struct analog_switch_driver_api *api = dev->api;
	    api->get_avg(dev, &val);
	    api->get_events(dev, &ev);

	    if (cbor_encode_int(&map, i) != CborNoError) return -EINVAL;
	    if (cbor_encoder_create_map(&map, &submap, 2) != CborNoError) return -EINVAL;

	    if (cbor_encode_text_string(&submap, ADC_KEY, strlen(ADC_KEY)) != CborNoError) return -EINVAL;
	    if (cbor_encode_int(&submap, val) != CborNoError) return -EINVAL;

	    if (cbor_encode_text_string(&submap, EV_KEY, strlen(EV_KEY)) != CborNoError) return -EINVAL;
	    if (cbor_encode_int(&submap, ev) != CborNoError) return -EINVAL;

	    if (cbor_encoder_close_container(&ce, &submap) != CborNoError) return -EINVAL;
    }

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int adc_avg_get(struct coap_resource *resource,
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

    r = prepare_adc_avg_payload(payload, MAX_COAP_PAYLOAD_LEN);
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

static int enable_for_dev(const struct device *dev, CborValue *map_val)
{
    const struct analog_switch_driver_api *api = dev->api;
    api->enable(dev);
    return 0;
}

static int adc_enable_post(struct coap_resource *resource,
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

    r = call_function_for_devs(&value, enable_for_dev);
    if (!r) rsp_code = COAP_RESPONSE_CODE_CHANGED;

    r = coap_server_send_ack(sock, addr, addr_len, id, rsp_code, token, tkl);
    return r;
}

static int prepare_adc_config_payload(uint8_t *payload, size_t len)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 2) != CborNoError) return -EINVAL;

    for (int i = 0; i < 2; i++) {
	    CborEncoder submap;
	    const struct device *dev = map_id_to_dev(i);
	    if (dev == NULL) return 0;

	    int iters = 0;
	    int threshold = 0;
	    int debounce = 0;
	    const struct analog_switch_driver_api *api = dev->api;
	    api->get_config(dev, &iters, &threshold, &debounce);

	    if (cbor_encode_int(&map, i) != CborNoError) return -EINVAL;
	    if (cbor_encoder_create_map(&map, &submap, 3) != CborNoError) return -EINVAL;

	    if (cbor_encode_text_string(&submap, ITER_KEY, strlen(ITER_KEY)) != CborNoError) return -EINVAL;
	    if (cbor_encode_int(&submap, iters) != CborNoError) return -EINVAL;

	    if (cbor_encode_text_string(&submap, THRES_KEY, strlen(THRES_KEY)) != CborNoError) return -EINVAL;
	    if (cbor_encode_int(&submap, threshold) != CborNoError) return -EINVAL;

	    if (cbor_encode_text_string(&submap, DEBOUNCE_KEY, strlen(DEBOUNCE_KEY)) != CborNoError) return -EINVAL;
	    if (cbor_encode_int(&submap, debounce) != CborNoError) return -EINVAL;

	    if (cbor_encoder_close_container(&ce, &submap) != CborNoError) return -EINVAL;
    }

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int adc_config_get(struct coap_resource *resource,
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

    r = prepare_adc_config_payload(payload, MAX_COAP_PAYLOAD_LEN);
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

static int set_config_for_dev(const struct device *dev, CborValue *value)
{
    bool updated = true;
    uint64_t req_det_iters;
    uint64_t req_det_thres;
    uint64_t req_deb_cnt;
    uint64_t req_iter_led;
    uint64_t req_deb_led;
    bool iter_led = false;
    bool debounce_led = false;
    CborValue map_val;
    CborError cbor_error;

    // Handle iters
    cbor_error = cbor_value_map_find_value(value, ITER_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_unsigned_integer(&map_val)) {
        cbor_error = cbor_value_get_uint64(&map_val, &req_det_iters);
        if ((cbor_error == CborNoError) && (req_det_iters <= INT_MAX)) {
		// Ready to be updated
        } else {
		return -EINVAL;
	}
    } else {
	    return -EINVAL;
    }

    // Handle threshold
    cbor_error = cbor_value_map_find_value(value, THRES_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_unsigned_integer(&map_val)) {
        cbor_error = cbor_value_get_uint64(&map_val, &req_det_thres);
        if ((cbor_error == CborNoError) && (req_det_thres <= UINT8_MAX)) {
		// Ready to be updated
        } else {
		return -EINVAL;
	}
    } else {
	    return -EINVAL;
    }

    // Handle debouncing counter
    cbor_error = cbor_value_map_find_value(value, DEBOUNCE_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_unsigned_integer(&map_val)) {
        cbor_error = cbor_value_get_uint64(&map_val, &req_deb_cnt);
        if ((cbor_error == CborNoError) && (req_det_thres <= UINT8_MAX)) {
		// Ready to be updated
        } else {
		return -EINVAL;
	}
    } else {
	    return -EINVAL;
    }

    // Handle debouncing led
    cbor_error = cbor_value_map_find_value(value, "dl", &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_unsigned_integer(&map_val)) {
        cbor_error = cbor_value_get_uint64(&map_val, &req_deb_led);
        if ((cbor_error == CborNoError) && req_deb_led) {
		debounce_led = true;
        }
    }
    // Handle iter led
    cbor_error = cbor_value_map_find_value(value, "il", &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_unsigned_integer(&map_val)) {
        cbor_error = cbor_value_get_uint64(&map_val, &req_iter_led);
        if ((cbor_error == CborNoError) && req_iter_led) {
		iter_led = true;
        }
    }

    if (updated) {
        const struct analog_switch_driver_api *api = dev->api;
        api->set_config(dev, req_det_iters, req_det_thres, req_deb_cnt, debounce_led, iter_led);

	return 0;
    }

    return -EINVAL;
}

static int adc_config_post(struct coap_resource *resource,
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

    r = call_function_for_devs(&value, set_config_for_dev);
    if (!r) rsp_code = COAP_RESPONSE_CODE_CHANGED;

    r = coap_server_send_ack(sock, addr, addr_len, id, rsp_code, token, tkl);
    return r;
}

static struct coap_resource * rsrcs_get(int sock)
{
    static const char * const fota_path [] = {"fota_req", NULL};
    static const char * const sd_path [] = {"sd", NULL};
    static const char * const prov_path[] = {"prov", NULL};
    static const char * const pulse_path[] = {"pulse", NULL};
    static const char * const adc_path[] = {"adc", NULL};
    static const char * const adc_avg_path[] = {"adc", "avg", NULL};
    static const char * const adc_enable_path[] = {"adc", "enable", NULL};
    static const char * const adc_config_path[] = {"adc", "config", NULL};
    static const char * const reboot_path[] = {"reboot", NULL};

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
	{ .post = pulse_post,
	  .path = pulse_path,
	},
	{ .get = adc_get,
	  .path = adc_path,
	},
	{ .get = adc_avg_get,
	  .path = adc_avg_path,
	},
	{ .post = adc_enable_post,
	  .path = adc_enable_path,
	},
	{ .get = adc_config_get,
	  .post = adc_config_post,
	  .path = adc_config_path,
	},
	{ .post = coap_reboot_post,
	  .path = reboot_path,
	},
        { .path = NULL } // Array terminator
    };

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
