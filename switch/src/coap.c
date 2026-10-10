/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap.h"

#include <cbor_utils.h>
#include <coap_fota.h>
#include <coap_reboot.h>
#include <coap_sd.h>
#include <coap_server.h>
#include "led.h"
#include "prov.h"

#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/coap.h>

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

static int handle_prov_post(zcbor_state_t *cd, enum coap_response_code *rsp_code, void *context)
{
    (void)context;
    int r = -EINVAL;
    bool updated = false;
    char str[PROV_LBL_MAX_LEN];
    int prov_int;
    bool prov_bool;

    // Handle rsrc0
    r = cbor_extract_from_map_string(cd, RSRC0_KEY, str, sizeof(str));
    if ((r >= 0) && (r < PROV_LBL_MAX_LEN)) {
        r = prov_set_rsrc_label(0, str);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle rsrc1
    r = cbor_extract_from_map_string(cd, RSRC1_KEY, str, sizeof(str));
    if ((r >= 0) && (r < PROV_LBL_MAX_LEN)) {
        r = prov_set_rsrc_label(1, str);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle output0
    r = cbor_extract_from_map_string(cd, OUT0_KEY, str, sizeof(str));
    if ((r >= 0) && (r < PROV_LBL_MAX_LEN)) {
        r = prov_set_output_rsrc_label(0, str);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle output1
    r = cbor_extract_from_map_string(cd, OUT1_KEY, str, sizeof(str));
    if ((r >= 0) && (r < PROV_LBL_MAX_LEN)) {
        r = prov_set_output_rsrc_label(1, str);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle analog0
    r = cbor_extract_from_map_bool(cd, ANALOG0_KEY, &prov_bool);
    if (r == 0) {
        r = prov_set_analog_enabled(0, prov_bool);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle analog1
    r = cbor_extract_from_map_bool(cd, ANALOG1_KEY, &prov_bool);
    if (r == 0) {
        r = prov_set_analog_enabled(1, prov_bool);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle threshold0
    r = cbor_extract_from_map_int(cd, THRESHOLD0_KEY, &prov_int);
    if (r == 0 && prov_int <= UINT16_MAX) {
        r = prov_set_analog_threshold(0, prov_int);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle threshold1
    r = cbor_extract_from_map_int(cd, THRESHOLD1_KEY, &prov_int);
    if (r == 0 && prov_int <= UINT16_MAX) {
        r = prov_set_analog_threshold(1, prov_int);

        if (r == 0) {
            updated = true;
        }
    }

    // Handle monostable
    r = cbor_extract_from_map_bool(cd, MONOSTABLE_KEY, &prov_bool);
    if (r == 0) {
        r = prov_set_monostable(prov_bool);

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
    int prov_int;
    bool prov_bool;

    if (!zcbor_map_start_encode(ce, 9)) return -EINVAL;

    label = prov_get_rsrc_label(0);
    if (!zcbor_tstr_put_lit(ce, RSRC0_KEY)) return -EINVAL;
    if (!zcbor_tstr_put_term(ce, label, 8)) return -EINVAL;

    label = prov_get_rsrc_label(1);
    if (!zcbor_tstr_put_lit(ce, RSRC1_KEY)) return -EINVAL;
    if (!zcbor_tstr_put_term(ce, label, 8)) return -EINVAL;

    label = prov_get_output_rsrc_label(0);
    if (!zcbor_tstr_put_lit(ce, OUT0_KEY)) return -EINVAL;
    if (!zcbor_tstr_put_term(ce, label, 8)) return -EINVAL;

    label = prov_get_output_rsrc_label(1);
    if (!zcbor_tstr_put_lit(ce, OUT1_KEY)) return -EINVAL;
    if (!zcbor_tstr_put_term(ce, label, 8)) return -EINVAL;

    prov_bool = prov_get_analog_enabled(0);
    if (!zcbor_tstr_put_lit(ce, ANALOG0_KEY)) return -EINVAL;
    if (!zcbor_bool_put(ce, prov_bool)) return -EINVAL;

    prov_bool = prov_get_analog_enabled(1);
    if (!zcbor_tstr_put_lit(ce, ANALOG1_KEY)) return -EINVAL;
    if (!zcbor_bool_put(ce, prov_bool)) return -EINVAL;

    prov_int = prov_get_analog_threshold(0);
    if (!zcbor_tstr_put_lit(ce, THRESHOLD0_KEY)) return -EINVAL;
    if (!zcbor_int32_put(ce, prov_int)) return -EINVAL;

    prov_int = prov_get_analog_threshold(1);
    if (!zcbor_tstr_put_lit(ce, THRESHOLD1_KEY)) return -EINVAL;
    if (!zcbor_int32_put(ce, prov_int)) return -EINVAL;

    prov_bool = prov_get_monostable();
    if (!zcbor_tstr_put_lit(ce, MONOSTABLE_KEY)) return -EINVAL;
    if (!zcbor_bool_put(ce, prov_bool)) return -EINVAL;

    if (!zcbor_map_end_encode(ce, 9)) return -EINVAL;

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

#define PULSE_KEY "p"

static int handle_pulse_post(zcbor_state_t *value,
	       	enum coap_response_code *rsp_code, void *context)
{
    int ret;
    int num_pulses;
    (void)context;

    *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;

    // Handle pulse
    ret = cbor_extract_from_map_int(value, PULSE_KEY, &num_pulses);
    if (ret == 0 && num_pulses <= ULONG_MAX) {
        led_set_pulses(num_pulses);
        *rsp_code = COAP_RESPONSE_CODE_CHANGED;
    }

    return ret;
}

static int pulse_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_simple_setter(sock, resource, request, addr, addr_len,
		    handle_pulse_post, NULL);
}

#include "analog_switch.h"
#define DEV_KEY "d"
#define ADC_KEY "a"
#define REF_KEY "r"
#define RET_KEY "r"
#define EV_KEY "e"
#define ITER_KEY "i"
#define SUM_ITER_KEY "si"
#define MIN_EV_KEY "me"
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

typedef int (*dev_callback)(const struct device *dev, zcbor_state_t *value,
        enum coap_response_code *rsp_code);

static int call_function_for_devs(zcbor_state_t *value,
        enum coap_response_code *rsp_code, dev_callback callback)
{
    const struct device *dev;
    int r = -EINVAL;
    int dev_id;

    // Handle device
    r = cbor_extract_from_map_int(value, "d", &dev_id);
    if (r == 0) {
        dev = map_id_to_dev(dev_id);
        if (dev != NULL) {
            r = callback(dev, value, rsp_code);
        } else {
            *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
            return -EINVAL;
        }
    } else {
        for (int i = 0; i < 2; ++i) {
            dev = map_id_to_dev(i);
            if (dev != NULL) {
                r = callback(dev, value, rsp_code);
                if (r) return r;
            } else {
                *rsp_code = COAP_RESPONSE_CODE_INTERNAL_ERROR;
                return -EINVAL;
            }
        }
    }

    return r;
}


static int prepare_adc_payload(uint8_t *payload, size_t len)
{
    ZCBOR_STATE_E(ce, 3, payload, len, 1);

    if (!zcbor_map_start_encode(ce, 2)) return -EINVAL;

    for (int i = 0; i < 2; i++) {
	    const struct device *dev = map_id_to_dev(i);
	    if (dev == NULL) return 0;

	    int r = 0;
	    uint16_t val = 0;
	    const struct analog_switch_driver_api *api = dev->api;
	    r = api->get(dev, &val);

        if (!zcbor_int32_put(ce, i)) return -EINVAL;
        if (!zcbor_map_start_encode(ce, 2)) return -EINVAL;

        if (!zcbor_tstr_put_lit(ce, RET_KEY)) return -EINVAL;
        if (!zcbor_int32_put(ce, r)) return -EINVAL;

        if (!zcbor_tstr_put_lit(ce, ADC_KEY)) return -EINVAL;
        if (!zcbor_int32_put(ce, val)) return -EINVAL;

        if (!zcbor_map_end_encode(ce, 2)) return -EINVAL;
    }

    if (!zcbor_map_end_encode(ce, 2)) return -EINVAL;

    return (size_t)(ce->payload - payload);
}

static int adc_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    int r = 0;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    size_t payload_len = 0;

    r = prepare_adc_payload(payload, MAX_COAP_PAYLOAD_LEN);
    if (r < 0) {
        return r;
    }
    payload_len = r;

    return coap_server_handle_simple_getter(sock, resource, request,
                    addr, addr_len, payload, payload_len);
}

static int prepare_adc_avg_payload(uint8_t *payload, size_t len)
{
    ZCBOR_STATE_E(ce, 3, payload, len, 1);

    if (!zcbor_map_start_encode(ce, 2)) return -EINVAL;

    for (int i = 0; i < 2; i++) {
	    const struct device *dev = map_id_to_dev(i);
	    if (dev == NULL) return 0;

	    uint16_t val = 0;
		uint16_t ref = 0;
	    uint16_t ev = 0;
	    const struct analog_switch_driver_api *api = dev->api;
	    api->get_avg(dev, &val, &ref);
	    api->get_events(dev, &ev);

        if (!zcbor_int32_put(ce, i)) return -EINVAL;
        if (!zcbor_map_start_encode(ce, 3)) return -EINVAL;

        if (!zcbor_tstr_put_lit(ce, ADC_KEY)) return -EINVAL;
        if (!zcbor_int32_put(ce, val)) return -EINVAL;

        if (!zcbor_tstr_put_lit(ce, REF_KEY)) return -EINVAL;
        if (!zcbor_int32_put(ce, ref)) return -EINVAL;

        if (!zcbor_tstr_put_lit(ce, EV_KEY)) return -EINVAL;
        if (!zcbor_int32_put(ce, ev)) return -EINVAL;

        if (!zcbor_map_end_encode(ce, 3)) return -EINVAL;
    }

    if (!zcbor_map_end_encode(ce, 2)) return -EINVAL;

    return (size_t)(ce->payload - payload);
}

static int adc_avg_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    int r = 0;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    size_t payload_len = 0;

    r = prepare_adc_avg_payload(payload, MAX_COAP_PAYLOAD_LEN);
    if (r < 0) {
        return r;
    }
    payload_len = r;

    return coap_server_handle_simple_getter(sock, resource, request,
                    addr, addr_len, payload, payload_len);
}

static int enable_for_dev(const struct device *dev, zcbor_state_t *map_val,
        enum coap_response_code *rsp_code)
{
    (void)map_val;

    const struct analog_switch_driver_api *api = dev->api;
    api->enable(dev);
    *rsp_code = COAP_RESPONSE_CODE_CHANGED;
    return 0;
}

static int handle_adc_enable_post(zcbor_state_t *value,
	       	enum coap_response_code *rsp_code, void *context)
{
    return call_function_for_devs(value, rsp_code, enable_for_dev);
}

static int adc_enable_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_simple_setter(sock, resource, request, addr, addr_len,
        handle_adc_enable_post, NULL);
}

static int prepare_adc_config_payload(uint8_t *payload, size_t len)
{
    ZCBOR_STATE_E(ce, 3, payload, len, 1);

    if (!zcbor_map_start_encode(ce, 2)) return -EINVAL;

    for (int i = 0; i < 2; i++) {
	    const struct device *dev = map_id_to_dev(i);
	    if (dev == NULL) return 0;

	    int iters = 0;
	    int sum_iters = 0;
	    int min_events = 0;
	    int threshold = 0;
	    int debounce = 0;
	    const struct analog_switch_driver_api *api = dev->api;
	    api->get_config(dev, &iters, &sum_iters, &min_events, &threshold, &debounce);

	    if (!zcbor_int32_put(ce, i)) return -EINVAL;
	    if (!zcbor_map_start_encode(ce, 5)) return -EINVAL;

	    if (!zcbor_tstr_put_lit(ce, ITER_KEY)) return -EINVAL;
	    if (!zcbor_int32_put(ce, iters)) return -EINVAL;

	    if (!zcbor_tstr_put_lit(ce, SUM_ITER_KEY)) return -EINVAL;
	    if (!zcbor_int32_put(ce, sum_iters)) return -EINVAL;

	    if (!zcbor_tstr_put_lit(ce, MIN_EV_KEY)) return -EINVAL;
	    if (!zcbor_int32_put(ce, min_events)) return -EINVAL;

	    if (!zcbor_tstr_put_lit(ce, THRES_KEY)) return -EINVAL;
	    if (!zcbor_int32_put(ce, threshold)) return -EINVAL;

	    if (!zcbor_tstr_put_lit(ce, DEBOUNCE_KEY)) return -EINVAL;
	    if (!zcbor_int32_put(ce, debounce)) return -EINVAL;

	    if (!zcbor_map_end_encode(ce, 5)) return -EINVAL;
    }

    if (!zcbor_map_end_encode(ce, 2)) return -EINVAL;

    return (size_t)(ce->payload - payload);
}

static int adc_config_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    int r = 0;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    size_t payload_len = 0;

    r = prepare_adc_config_payload(payload, MAX_COAP_PAYLOAD_LEN);
    if (r < 0) {
        return r;
    }
    payload_len = r;

    return coap_server_handle_simple_getter(sock, resource, request,
                    addr, addr_len, payload, payload_len);

}

static int set_config_for_dev(const struct device *dev, zcbor_state_t *value,
        enum coap_response_code *rsp_code)
{
    int r;
    bool updated = false;
    int req_det_iters;
    int req_sum_iters;
    int req_min_events;
    int req_det_thres;
    int req_deb_cnt;
    int req_iter_led;
    int req_deb_led;
    bool iter_led = false;
    bool debounce_led = false;

    const struct analog_switch_driver_api *api = dev->api;
    r = api->get_config(dev, &req_det_iters, &req_sum_iters, &req_min_events,
                        &req_det_thres, &req_deb_cnt);
    if (r != 0) {
        *rsp_code = COAP_RESPONSE_CODE_INTERNAL_ERROR;
        return r;
    }

    r = cbor_extract_from_map_int(value, ITER_KEY, &req_det_iters);
    if (r == 0) {
        if (req_det_iters < 0) {
            *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
            return -EINVAL;
        }

        updated = true;
    }

    r = cbor_extract_from_map_int(value, SUM_ITER_KEY, &req_sum_iters);
    if (r == 0) {
        if (req_sum_iters < 0) {
            *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
            return -EINVAL;
        }

        updated = true;
    }

    r = cbor_extract_from_map_int(value, MIN_EV_KEY, &req_min_events);
    if (r == 0) {
        if (req_min_events < 0) {
            *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
            return -EINVAL;
        }

        updated = true;
    }

    // Handle threshold
    r = cbor_extract_from_map_int(value, THRES_KEY, &req_det_thres);
    if (r == 0) {
        if (req_det_thres < 0) {
            *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
            return -EINVAL;
        }

        updated = true;
    }

    // Handle debouncing counter
    r = cbor_extract_from_map_int(value, DEBOUNCE_KEY, &req_deb_cnt);
    if (r == 0) {
        if (req_deb_cnt < 0 || req_deb_cnt > UINT8_MAX) {
            *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;
            return -EINVAL;
        }

        updated = true;
    }

    // Handle debouncing led
    r = cbor_extract_from_map_int(value, "dl", &req_deb_led);
    if (r == 0 && req_deb_led > 0) {
        debounce_led = true;
        updated = true;
    }

    // Handle iter led
    r = cbor_extract_from_map_int(value, "il", &req_iter_led);
    if (r == 0 && req_iter_led > 0) {
        iter_led = true;
        updated = true;
    }

    if (updated) {
        api->set_config(dev, req_det_iters, req_sum_iters, req_min_events,
                        req_det_thres, req_deb_cnt, debounce_led, iter_led);
        *rsp_code = COAP_RESPONSE_CODE_CHANGED;

        return 0;
    } else {
        *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;

        return -EINVAL;
    }
}

static int handle_adc_config_post(zcbor_state_t *value,
	       	enum coap_response_code *rsp_code, void *context)
{
    (void)context;

    return call_function_for_devs(value, rsp_code, set_config_for_dev);
}

static int adc_config_post(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_simple_setter(sock, resource, request ,addr, addr_len,
		    handle_adc_config_post, NULL);
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
