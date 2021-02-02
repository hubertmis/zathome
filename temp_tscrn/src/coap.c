/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap.h"

#include <errno.h>
#include <stdint.h>

#include "data_dispatcher.h"
#include "prov.h"

#include <zephyr.h>
#include <net/socket.h>
#include <net/coap.h>
#include <net/fota_download.h>
#include <random/rand32.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>

#define MY_COAP_PORT 5683
#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 64

#define MAX_FOTA_PAYLOAD_LEN 64
#define MAX_FOTA_PATH_LEN 16

#define COAP_CONTENT_FORMAT_TEXT 0
#define COAP_CONTENT_FORMAT_CBOR 60

#define COAP_THREAD_STACK_SIZE 2048
#define COAP_THREAD_PRIO       0
static void coap_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(coap_thread_id, COAP_THREAD_STACK_SIZE,
                coap_thread_process, NULL, NULL, NULL,
                COAP_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

static int sock;

static int start_coap_server(void)
{
    struct sockaddr_in6 addr6;
    int r;

    memset(&addr6, 0, sizeof(addr6));
    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons(MY_COAP_PORT);

    sock = socket(addr6.sin6_family, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return -errno;
    }

    r = bind(sock, (struct sockaddr *)&addr6, sizeof(addr6));
    if (r < 0) {
        return -errno;
    }

    return 0;
}

static int send_coap_reply(struct coap_packet *cpkt,
               const struct sockaddr *addr,
               socklen_t addr_len)
{
    int r;

    r = sendto(sock, cpkt->data, cpkt->offset, 0, addr, addr_len);
    if (r < 0) {
        r = -errno;
    }

    return r;
}

static int send_ack(const struct sockaddr *addr, socklen_t addr_len,
                    uint16_t id, enum coap_response_code code, uint8_t *token, uint8_t tkl)
{
    uint8_t *data;
    int r = 0;
    struct coap_packet response;

    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
    if (!data) {
        return -ENOMEM;
    }

    r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
                 1, COAP_TYPE_ACK, tkl, token, code, id);
    if (r < 0) {
        goto end;
    }

    r = send_coap_reply(&response, addr, addr_len);

end:
    k_free(data);

    return r;
}


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

static int encode_temp(CborEncoder *enc, uint16_t temp)
{
    CborError err;
    CborEncoder arr;

    if ((err = cbor_encode_tag(enc, TAG_DECIMAL_FRACTION)) != CborNoError) return err;
    if ((err = cbor_encoder_create_array(enc, &arr, 2)) != CborNoError) return err;
    if ((err = cbor_encode_negative_int(&arr, 0)) != CborNoError) return err;
    if ((err = cbor_encode_int(&arr, temp)) != CborNoError) return err;
    if ((err = cbor_encoder_close_container(enc, &arr)) != CborNoError) return err;

    return CborNoError;
}

static int prepare_temp_payload(uint8_t *payload, size_t len, data_loc_t loc)
{
    const data_dispatcher_publish_t *meas, *sett, *output, *ctlr;
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;
    CborEncoder cnt_map;
    data_ctlr_mode_t ctlr_mode;
    const char *cm_str;

    data_dispatcher_get(DATA_TEMP_MEASUREMENT, loc, &meas);
    data_dispatcher_get(DATA_TEMP_SETTING, loc, &sett);
    data_dispatcher_get(DATA_OUTPUT, loc, &output);
    data_dispatcher_get(DATA_CONTROLLER, loc, &ctlr);

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 4) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, MEAS_KEY, strlen(MEAS_KEY)) != CborNoError) return -EINVAL;
    if (encode_temp(&map, meas->temp_measurement) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, SETT_KEY, strlen(SETT_KEY)) != CborNoError) return -EINVAL;
    if (encode_temp(&map, sett->temp_setting) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, OUT_KEY, strlen(OUT_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, output->output) != CborNoError) return -EINVAL;

    ctlr_mode = ctlr->controller.mode;
    cm_str = cnt_val_map[ctlr_mode];
    if (cbor_encode_text_string(&map, CNT_KEY, strlen(CNT_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encoder_create_map(&map, &cnt_map, ctlr_mode == DATA_CTLR_ONOFF ? 2 : 3) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&cnt_map, CNT_KEY, strlen(CNT_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&cnt_map, cm_str, strlen(cm_str)) != CborNoError) return -EINVAL;

    if (ctlr_mode == DATA_CTLR_ONOFF) {
        if (cbor_encode_text_string(&cnt_map, HYST_KEY, strlen(HYST_KEY)) != CborNoError) return -EINVAL;
        if (cbor_encode_int(&cnt_map, ctlr->controller.hysteresis) != CborNoError) return -EINVAL;
    } else {
        if (cbor_encode_text_string(&cnt_map, P_KEY, strlen(P_KEY)) != CborNoError) return -EINVAL;
        if (cbor_encode_int(&cnt_map, ctlr->controller.p) != CborNoError) return -EINVAL;

        if (cbor_encode_text_string(&cnt_map, I_KEY, strlen(I_KEY)) != CborNoError) return -EINVAL;
        if (cbor_encode_int(&cnt_map, ctlr->controller.i) != CborNoError) return -EINVAL;
    }

    if (cbor_encoder_close_container(&map, &cnt_map) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int temp_handler(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len,
             data_loc_t loc)
{
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
        // TODO: Shall I respond with an error?
        return -EINVAL;
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

    r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
            COAP_CONTENT_FORMAT_CBOR);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload_marker(&response);
    if (r < 0) {
        goto end;
    }

    r = prepare_temp_payload(payload, MAX_COAP_PAYLOAD_LEN, loc);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload(&response, payload, r);
    if (r < 0) {
        goto end;
    }

    r = send_coap_reply(&response, addr, addr_len);

end:
    k_free(data);

    return r;
}

static int temp_put(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len,
             data_loc_t loc)
{
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
        // TODO: Shall I respond with an error?
        return -EINVAL;
    }

    // TODO: At least verify if destination address is site local

    r = coap_find_options(request, COAP_OPTION_CONTENT_FORMAT, &option, 1); 
    if (r != 1) {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (coap_option_value_to_int(&option) != COAP_CONTENT_FORMAT_CBOR) {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT, token, tkl);
        return -EINVAL;
    }

    payload = coap_packet_get_payload(request, &payload_len);
    if (!payload) {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    CborError cbor_error;
    CborParser parser;
    CborValue value;
    struct cbor_buf_reader reader;

    cbor_buf_reader_init(&reader, payload, payload_len);

    cbor_error = cbor_parser_init(&reader.r, 0, &parser, &value);
    if (cbor_error != CborNoError) {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (!cbor_value_is_map(&value)) {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    // Handle temperature setting
    CborValue sett_cbor_el;

    cbor_error = cbor_value_map_find_value(&value, SETT_KEY, &sett_cbor_el);
    if ((cbor_error == CborNoError) && cbor_value_is_valid(&sett_cbor_el)) {
        if (cbor_value_is_tag(&sett_cbor_el)) {
            CborTag sett_tag;

            cbor_error = cbor_value_get_tag(&sett_cbor_el, &sett_tag);
            if ((cbor_error != CborNoError) || (sett_tag != TAG_DECIMAL_FRACTION)) {
                send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
                return -EINVAL;
            }

            cbor_error = cbor_value_skip_tag(&sett_cbor_el);
            if ((cbor_error != CborNoError) || !cbor_value_is_array(&sett_cbor_el)) {
                send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
                return -EINVAL;
            }

            size_t arr_len;
            cbor_error = cbor_value_get_array_length(&sett_cbor_el, &arr_len);
            if ((cbor_error != CborNoError) || (arr_len != 2)) {
                send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
                return -EINVAL;
            }

            CborValue frac_arr;
            cbor_error = cbor_value_enter_container(&sett_cbor_el, &frac_arr);
            if ((cbor_error != CborNoError) || !cbor_value_is_integer(&frac_arr)) {
                send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
                return -EINVAL;
            }

            int integer;
            cbor_error = cbor_value_get_int(&frac_arr, &integer);
            if ((cbor_error != CborNoError) || (integer != -1)) {
                send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
                return -EINVAL;
            }

            cbor_error = cbor_value_advance_fixed(&frac_arr);
            if ((cbor_error != CborNoError) || !cbor_value_is_integer(&frac_arr)) {
                send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
                return -EINVAL;
            }

            cbor_error = cbor_value_get_int(&frac_arr, &integer);
            if (cbor_error != CborNoError) {
                send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
                return -EINVAL;
            }

            data_dispatcher_publish_t sett = {
                .loc = loc,
                .type = DATA_TEMP_SETTING,
                .temp_setting = integer,
            };
            data_dispatcher_publish(&sett);

            rsp_code = COAP_RESPONSE_CODE_CHANGED;
        } else {
            // TODO: Handle integer, possibly float as well
            send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
            return -EINVAL;
        }
    }

    // Handle controller
    CborValue cnt_cbor_el;

    cbor_error = cbor_value_map_find_value(&value, CNT_KEY, &cnt_cbor_el);
    if ((cbor_error == CborNoError) && cbor_value_is_valid(&cnt_cbor_el)) {
        if (!cbor_value_is_map(&cnt_cbor_el)) {
            send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
            return -EINVAL;
        }

        bool updated = false;
        const data_dispatcher_publish_t *ctlr;
        data_dispatcher_publish_t new_ctlr;
        data_dispatcher_get(DATA_CONTROLLER, loc, &ctlr);
        new_ctlr = *ctlr;

        CborValue ctlr_mode_cbor_el;

        // Handle controller mode
        cbor_error = cbor_value_map_find_value(&cnt_cbor_el, CNT_KEY, &ctlr_mode_cbor_el);
        if ((cbor_error == CborNoError) && cbor_value_is_text_string(&ctlr_mode_cbor_el)) {
            const size_t max_str_len = 6;
            char str[max_str_len];
            size_t str_len = max_str_len;

            cbor_error = cbor_value_copy_text_string(&ctlr_mode_cbor_el, str, &str_len, NULL);
            if (cbor_error == CborNoError) {
                for (size_t i = 0; i < sizeof(cnt_val_map) / sizeof(cnt_val_map[0]); ++i) {
                    if (strncmp(cnt_val_map[i], str, max_str_len) == 0) {
                        new_ctlr.controller.mode = i;
                        updated = true;
                        break;
                    }
                }
            }
        }

        // Handle hysteresis
        cbor_error = cbor_value_map_find_value(&cnt_cbor_el, HYST_KEY, &ctlr_mode_cbor_el);
        if ((cbor_error == CborNoError) && cbor_value_is_integer(&ctlr_mode_cbor_el)) {
            int hyst_val;
            cbor_error = cbor_value_get_int(&ctlr_mode_cbor_el, &hyst_val);
            if (cbor_error == CborNoError) {
                new_ctlr.controller.hysteresis = hyst_val;
                updated = true;
            }
        }

        // Handle P
        cbor_error = cbor_value_map_find_value(&cnt_cbor_el, P_KEY, &ctlr_mode_cbor_el);
        if ((cbor_error == CborNoError) && cbor_value_is_integer(&ctlr_mode_cbor_el)) {
            int p_val;
            cbor_error = cbor_value_get_int(&ctlr_mode_cbor_el, &p_val);
            if (cbor_error == CborNoError) {
                new_ctlr.controller.p = p_val;
                updated = true;
            }
        }

        // Handle I
        cbor_error = cbor_value_map_find_value(&cnt_cbor_el, I_KEY, &ctlr_mode_cbor_el);
        if ((cbor_error == CborNoError) && cbor_value_is_integer(&ctlr_mode_cbor_el)) {
            int i_val;
            cbor_error = cbor_value_get_int(&ctlr_mode_cbor_el, &i_val);
            if (cbor_error == CborNoError) {
                new_ctlr.controller.i = i_val;
                updated = true;
            }
        }

        if (updated) {
            rsp_code = COAP_RESPONSE_CODE_CHANGED;
            data_dispatcher_publish(&new_ctlr);
        }
    }

    r = send_ack(addr, addr_len, id, rsp_code, token, tkl);
    return r;
}

static int temp_local_get(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    return temp_handler(resource, request, addr, addr_len, DATA_LOC_LOCAL);
}

static int temp_local_put(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    return temp_put(resource, request, addr, addr_len, DATA_LOC_LOCAL);
}

static int temp_remote_get(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    return temp_handler(resource, request, addr, addr_len, DATA_LOC_REMOTE);
}

static int temp_remote_put(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    return temp_put(resource, request, addr, addr_len, DATA_LOC_REMOTE);
}

static int fota_put(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[8];
    const uint8_t *payload;
    uint16_t payload_len;
    char url[MAX_FOTA_PAYLOAD_LEN];
    char *path = NULL;
    static char fota_path[MAX_FOTA_PATH_LEN];

    code = coap_header_get_code(request);
    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    if (type != COAP_TYPE_CON) {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    // TODO: At least verify if destination address is site local

    payload = coap_packet_get_payload(request, &payload_len);
    if (!payload) {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (payload_len >= sizeof(url)) {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_REQUEST_TOO_LARGE, token, tkl);
        return -EINVAL;
    }

    // Start fota using sent URL
    memcpy(url, payload, payload_len);
    url[payload_len] = '\0';

    char *scheme_end = strstr(url, "://");
    if (scheme_end) {
        char *host = scheme_end + 3;

        char *host_end = strchr(host, '/');
        if (host_end) {
            *host_end = '\0';
            strncpy(fota_path, host_end + 1, sizeof(fota_path));
            path = fota_path;
        }
    }

    int fota_result = fota_download_start(url, path, -1, NULL, 0);

    if (fota_result) {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_CHANGED, token, tkl);
    return 0;

}

static int fota_get(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[8];
    uint8_t *data;
    int r = 0;
    struct coap_packet response;
    char payload[] = CONFIG_MCUBOOT_IMAGE_VERSION;

    code = coap_header_get_code(request);
    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    if (type != COAP_TYPE_CON) {
        return -EINVAL;
    }

    // TODO: At least verify if destination address is site local

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
            COAP_CONTENT_FORMAT_TEXT);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload_marker(&response);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload(&response, payload, sizeof(payload));
    if (r < 0) {
        goto end;
    }

    r = send_coap_reply(&response, addr, addr_len);

end:
    k_free(data);

    return r;
}

#define RSRC0_KEY "r0"
#define RSRC1_KEY "r1"
#define OUT0_KEY "o0"

static int prov_put(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
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
        // TODO: Shall I respond with an error?
        return -EINVAL;
    }

    // TODO: At least verify if destination address is site local

    r = coap_find_options(request, COAP_OPTION_CONTENT_FORMAT, &option, 1); 
    if (r != 1) {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (coap_option_value_to_int(&option) != COAP_CONTENT_FORMAT_CBOR) {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT, token, tkl);
        return -EINVAL;
    }

    payload = coap_packet_get_payload(request, &payload_len);
    if (!payload) {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
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
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (!cbor_value_is_map(&value)) {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
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
                r = prov_set_rsrc_label(DATA_LOC_LOCAL, str);

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
                r = prov_set_rsrc_label(DATA_LOC_REMOTE, str);

                if (r == 0) {
                    updated = true;
                }
            }
        }
    }

    // Handle out0
    cbor_error = cbor_value_map_find_value(&value, OUT0_KEY, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_text_string(&map_val)) {
        size_t str_len;

        cbor_error = cbor_value_get_string_length(&map_val, &str_len);
        if ((cbor_error == CborNoError) && (str_len < PROV_LBL_MAX_LEN)) {
            char str[PROV_LBL_MAX_LEN];
            str_len = PROV_LBL_MAX_LEN;

            cbor_error = cbor_value_copy_text_string(&map_val, str, &str_len, NULL);
            if (cbor_error == CborNoError) {
                r = prov_set_loc_output_label(str);

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

    r = send_ack(addr, addr_len, id, rsp_code, token, tkl);
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

    if (cbor_encoder_create_map(&ce, &map, 3) != CborNoError) return -EINVAL;

    label = prov_get_rsrc_label(DATA_LOC_LOCAL);
    if (cbor_encode_text_string(&map, RSRC0_KEY, strlen(RSRC0_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&map, label, strlen(label)) != CborNoError) return -EINVAL;

    label = prov_get_rsrc_label(DATA_LOC_REMOTE);
    if (cbor_encode_text_string(&map, RSRC1_KEY, strlen(RSRC1_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&map, label, strlen(label)) != CborNoError) return -EINVAL;

    label = prov_get_loc_output_label();
    if (cbor_encode_text_string(&map, OUT0_KEY, strlen(OUT0_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&map, label, strlen(label)) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int prov_get(struct coap_resource *resource,
        struct coap_packet *request,
        struct sockaddr *addr, socklen_t addr_len)
{
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

    // TODO: At least verify if destination address is site local

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

    r = send_coap_reply(&response, addr, addr_len);

end:
    k_free(data);

    return r;
}

#define SD_FLT_NAME "name"
#define SD_FLT_TYPE "type"
#define SD_RSRC "sd"
#define SD_NAME_MAX_LEN 8
#define SD_TYPE_MAX_LEN 8

static bool filter_sd_req(const uint8_t *payload, uint16_t payload_len)
{
    CborError cbor_error;
    CborParser parser;
    CborValue value;
    struct cbor_buf_reader reader;

    cbor_buf_reader_init(&reader, payload, payload_len);

    cbor_error = cbor_parser_init(&reader.r, 0, &parser, &value);
    if (cbor_error != CborNoError) {
        return false;
    }

    if (!cbor_value_is_map(&value)) {
        return false;
    }

    CborValue map_val;

    // Handle name
    cbor_error = cbor_value_map_find_value(&value, SD_FLT_NAME, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_text_string(&map_val)) {
        size_t str_len;

        cbor_error = cbor_value_get_string_length(&map_val, &str_len);
        if ((cbor_error == CborNoError) && (str_len < SD_NAME_MAX_LEN)) {
            char str[SD_NAME_MAX_LEN];
            str_len = SD_NAME_MAX_LEN;

            cbor_error = cbor_value_copy_text_string(&map_val, str, &str_len, NULL);
            if (cbor_error == CborNoError) {
                bool found = false;

                for (int i = 0; i < DATA_LOC_NUM; ++i) {
                    if (strncmp(str, prov_get_rsrc_label(i), SD_NAME_MAX_LEN) == 0) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    return false;
                }
            }
        }
    }

    // Handle type
    cbor_error = cbor_value_map_find_value(&value, SD_FLT_TYPE, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_text_string(&map_val)) {
        size_t str_len;

        cbor_error = cbor_value_get_string_length(&map_val, &str_len);
        if ((cbor_error == CborNoError) && (str_len < SD_TYPE_MAX_LEN)) {
            char str[SD_TYPE_MAX_LEN];
            str_len = SD_TYPE_MAX_LEN;

            cbor_error = cbor_value_copy_text_string(&map_val, str, &str_len, NULL);
            if (cbor_error == CborNoError) {
                if (strncmp(str, "tempcnt", SD_TYPE_MAX_LEN) != 0) {
                    return false;
                }
            }
        }
    }

    return true;
}

static int prepare_sd_rsp_payload(uint8_t *payload, size_t len)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;
    const char *rsrcs[DATA_LOC_NUM];
    int num_rsrcs = 0;

    for (int i = 0; i < DATA_LOC_NUM; ++i) {
        rsrcs[i] = prov_get_rsrc_label(i);

        if (rsrcs[i] && strlen(rsrcs[i])) {
            num_rsrcs++;
        }
    }

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, num_rsrcs) != CborNoError) return -EINVAL;

    for (int i = 0; i < DATA_LOC_NUM; ++i) {
        if (rsrcs[i] && strlen(rsrcs[i])) {
            CborEncoder rsrc_map;
            if (cbor_encode_text_string(&map, rsrcs[i], strlen(rsrcs[i])) != CborNoError) return -EINVAL;
            if (cbor_encoder_create_map(&map, &rsrc_map, 1) != CborNoError) return -EINVAL;

            if (cbor_encode_text_string(&rsrc_map, SD_FLT_TYPE, strlen(SD_FLT_TYPE)) != CborNoError) return -EINVAL;
            if (cbor_encode_text_string(&rsrc_map, "tempcnt", strlen("tempcnt")) != CborNoError) return -EINVAL;

            if (cbor_encoder_close_container(&map, &rsrc_map) != CborNoError) return -EINVAL;
        }
    }

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int send_sd_rsp(const struct sockaddr *addr, socklen_t addr_len,
                       uint8_t *token, uint8_t tkl)
{
    uint8_t *data;
    int r = 0;
    struct coap_packet response;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];

    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
    if (!data) {
        return -ENOMEM;
    }

    r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
                 1, COAP_TYPE_NON_CON, tkl, token,
                 COAP_RESPONSE_CODE_CONTENT, coap_next_id());
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

    r = prepare_sd_rsp_payload(payload, MAX_COAP_PAYLOAD_LEN);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload(&response, payload, r);
    if (r < 0) {
        goto end;
    }

    r = send_coap_reply(&response, addr, addr_len);

end:
    k_free(data);

    return r;
}

static int sd_get(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[8];
    int r = 0;
    struct coap_option option;
    const uint8_t *payload;
    uint16_t payload_len;

    bool opt_cf_present = false;
    bool opt_cf_correct = false;
    bool payload_present = false;
    bool filter_passed = true;

    code = coap_header_get_code(request);
    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    if (type != COAP_TYPE_NON_CON) {
        return -EINVAL;
    }

    // TODO: At least verify if destination address is site local

    r = coap_find_options(request, COAP_OPTION_CONTENT_FORMAT, &option, 1); 
    if (r == 1) {
        opt_cf_present = true;
    }

    if (opt_cf_present && (coap_option_value_to_int(&option) == COAP_CONTENT_FORMAT_CBOR)) {
        opt_cf_correct = true;
    }

    payload = coap_packet_get_payload(request, &payload_len);
    if (payload) {
        payload_present = true;
    }

    if (opt_cf_present && (!opt_cf_correct || !payload_present)) {
        return -EINVAL;
    }

    if (opt_cf_present && opt_cf_correct && payload_present) {
        filter_passed = filter_sd_req(payload, payload_len);
    }

    if (filter_passed) {
        k_sleep(K_MSEC(sys_rand32_get() % 512));
        r = send_sd_rsp(addr, addr_len, token, tkl);
    } else {
        r = 0;
    }

    return r;
}

static int sd_test(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[8];
    int r = 0;
    enum coap_response_code rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;

    code = coap_header_get_code(request);
    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    if (type != COAP_TYPE_CON) {
        return -EINVAL;
    }
    
    r = coap_sd_start("hb", "shcnt", NULL);
    if (r >= 0) {
        rsp_code = COAP_RESPONSE_CODE_CHANGED;
    }

    r = send_ack(addr, addr_len, id, rsp_code, token, tkl);

    return r;
}

static void process_coap_request(uint8_t *data, uint16_t data_len,
                 struct sockaddr *client_addr,
                 socklen_t client_addr_len)
{
    struct coap_packet request;
    struct coap_option options[16] = { 0 };
    uint8_t opt_num = 16U;
    int r;

    struct coap_resource resources[] = {
        { .get = fota_get,
          .put = fota_put,
          .path = (const char * const []){"fota_req", NULL},
        },
        {
            .get = prov_get,
            .put = prov_put,
            .path = (const char * const []){"prov", NULL},
        },
        { .get = temp_remote_get,
          .put = temp_remote_put,
          .path = (const char * const []){prov_get_rsrc_label(DATA_LOC_REMOTE), NULL},
        },
        { .get = temp_local_get,
          .put = temp_local_put,
          .path = (const char * const []){prov_get_rsrc_label(DATA_LOC_LOCAL), NULL},
        },
        { .get = sd_get,
          .path = (const char * const []){"sd", NULL},
        },
        // TODO: Remove this test and replace it with real discovery of output
        { .get = sd_test,
          .path = (const char * const []){"sdtest", NULL},
        },
        { .path = NULL } // Array terminator
    };


    r = coap_packet_parse(&request, data, data_len, options, opt_num);
    if (r < 0) {
        return;
    }

    r = coap_handle_request(&request, resources, options, opt_num,
                client_addr, client_addr_len);
    if (r == -ENOENT) {
        uint16_t id;
        uint8_t tkl;
        uint8_t token[8];

        id = coap_header_get_id(&request);
        tkl = coap_header_get_token(&request, token);

        send_ack(client_addr, client_addr_len, id, COAP_RESPONSE_CODE_NOT_FOUND, token, tkl);
    }
    if (r < 0) {
        return;
    }
}

static int process_client_request(void)
{
    int received;
    struct sockaddr client_addr;
    socklen_t client_addr_len;
    uint8_t request[MAX_COAP_MSG_LEN];

    do {
        client_addr_len = sizeof(client_addr);
        received = recvfrom(sock, request, sizeof(request), 0,
                    &client_addr, &client_addr_len);
        if (received < 0) {
            return -errno;
        }

        process_coap_request(request, received, &client_addr,
                     client_addr_len);
    } while (true);

    return 0;
}

static void coap_thread_process(void *a1, void *a2, void *a3)
{
    (void)a1;
    (void)a2;
    (void)a3;

    while (1) {
        process_client_request();
    }
}

void coap_init(void)
{
    start_coap_server();

    k_thread_start(coap_thread_id);
}

static int prepare_sd_req_payload(uint8_t *payload, size_t len, const char *name, const char *type)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;

    bool name_known = (name != NULL) && (strlen(name) > 0);
    bool type_known = (type != NULL) && (strlen(type) > 0);

    int num_filters = 0;

    if (name_known) num_filters++;
    if (type_known) num_filters++;

    if (!num_filters) {
        // There are no filters to add as payload
        return 0;
    }

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, num_filters) != CborNoError) return -EINVAL;

    if (name_known) {
        if (cbor_encode_text_string(&map, SD_FLT_NAME, strlen(SD_FLT_NAME)) != CborNoError) return -EINVAL;
        if (cbor_encode_text_string(&map, name, strlen(name)) != CborNoError) return -EINVAL;
    }

    if (type_known) {
        if (cbor_encode_text_string(&map, SD_FLT_TYPE, strlen(SD_FLT_TYPE)) != CborNoError) return -EINVAL;
        if (cbor_encode_text_string(&map, type, strlen(type)) != CborNoError) return -EINVAL;
    }

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int coap_sd_send_req(const char *name, const char *type, int sock)
{
    int r;
    struct coap_packet cpkt;
    uint8_t *data;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    struct sockaddr_in6 addr = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(MY_COAP_PORT),
        .sin6_addr = {
            .s6_addr = {0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}
        },
    };

    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
    if (!data) {
        return -ENOMEM;
    }

    r = coap_packet_init(&cpkt, data, MAX_COAP_MSG_LEN,
                 1, COAP_TYPE_NON_CON, 4, coap_next_token(),
                 COAP_METHOD_GET, coap_next_id());
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_option(&cpkt, COAP_OPTION_URI_PATH, SD_RSRC, strlen(SD_RSRC));
    if (r < 0) {
        goto end;
    }

    r = coap_append_option_int(&cpkt, COAP_OPTION_CONTENT_FORMAT,
            COAP_CONTENT_FORMAT_CBOR);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload_marker(&cpkt);
    if (r < 0) {
        goto end;
    }

    r = prepare_sd_req_payload(payload, MAX_COAP_PAYLOAD_LEN, name, type);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload(&cpkt, payload, r);
    if (r < 0) {
        goto end;
    }

    r = sendto(sock, cpkt.data, cpkt.offset, 0, (struct sockaddr *)&addr, sizeof(addr));
    if (r < 0) {
        r = -errno;
    }

end:
    k_free(data);

    return r;
}

static int coap_sd_process_rsp(uint8_t *data, size_t data_len,
                               const coap_sd_found cb,
                               const struct sockaddr *addr,
                               const socklen_t *addr_len,
                               const char *name,
                               const char *type)
{
    struct coap_packet rsp;
    int r;
    uint8_t coap_type;
    struct coap_option option;
    const uint8_t *payload;
    uint16_t payload_len;

    r = coap_packet_parse(&rsp, data, data_len, NULL, 0);
    if (r < 0) {
        return r;
    }

    coap_type = coap_header_get_type(&rsp);

    if (coap_type != COAP_TYPE_NON_CON) {
        return -EINVAL;
    }

    // TODO: At least verify if destination address is site local

    r = coap_find_options(&rsp, COAP_OPTION_CONTENT_FORMAT, &option, 1); 
    if (r != 1) {
        return -EINVAL;
    }

    if (coap_option_value_to_int(&option) != COAP_CONTENT_FORMAT_CBOR) {
        return -EINVAL;
    }

    payload = coap_packet_get_payload(&rsp, &payload_len);
    if (!payload) {
        return -EINVAL;
    }

    CborError cbor_error;
    CborParser parser;
    CborValue value;
    struct cbor_buf_reader reader;
    char rcvd_name[SD_NAME_MAX_LEN];
    char rcvd_type[SD_TYPE_MAX_LEN];

    cbor_buf_reader_init(&reader, payload, payload_len);

    cbor_error = cbor_parser_init(&reader.r, 0, &parser, &value);
    if (cbor_error != CborNoError) {
        return -EINVAL;
    }

    if (!cbor_value_is_map(&value)) {
        return -EINVAL;
    }

    CborValue map_val;

    // Handle name
    cbor_error = cbor_value_map_find_value(&value, SD_FLT_NAME, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_text_string(&map_val)) {
        size_t str_len;

        cbor_error = cbor_value_get_string_length(&map_val, &str_len);
        if ((cbor_error != CborNoError) || (str_len >= SD_NAME_MAX_LEN)) {
            return -EINVAL;
        }

        str_len = SD_NAME_MAX_LEN;

        cbor_error = cbor_value_copy_text_string(&map_val, rcvd_name, &str_len, NULL);
        if (cbor_error != CborNoError) {
            return -EINVAL;
        }
    } else {
        // Missing name
        return -EINVAL;
    }

    // Handle type
    cbor_error = cbor_value_map_find_value(&value, SD_FLT_TYPE, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_text_string(&map_val)) {
        size_t str_len;

        cbor_error = cbor_value_get_string_length(&map_val, &str_len);
        if ((cbor_error != CborNoError) || (str_len >= SD_NAME_MAX_LEN)) {
            return -EINVAL;
        }

        str_len = SD_TYPE_MAX_LEN;

        cbor_error = cbor_value_copy_text_string(&map_val, rcvd_type, &str_len, NULL);
        if (cbor_error != CborNoError) {
            return -EINVAL;
        }
    } else {
        // Missing type
        return -EINVAL;
    }

    if (name && strlen(name)) {
        if (strncmp(name, rcvd_name, SD_NAME_MAX_LEN) != 0) {
            return -EINVAL;
        }
    }

    if (type && strlen(type)) {
        if (strncmp(type, rcvd_type, SD_TYPE_MAX_LEN) != 0) {
            return -EINVAL;
        }
    }

    if (cb) {
        cb(addr, addr_len, rcvd_name, rcvd_type);
    }

    return 0;
}

static int coap_sd_receive_rsp(int sock,
                               const coap_sd_found cb,
                               const char *name,
                               const char *type)
{
    int r;
    struct sockaddr addr;
    socklen_t addr_len;
    uint8_t response[MAX_COAP_MSG_LEN];

    while (1) {
        r = recvfrom(sock, response, sizeof(response), 0, &addr, &addr_len);
        if (r < 0) {
            if (errno == EAGAIN) {
                // Expecting timeout
                return 0;
            } else {
                return -errno;
            }
        }

        coap_sd_process_rsp(response, r, cb, &addr, &addr_len, name, type);
    }
}

int coap_sd_start(const char *name, const char *type, coap_sd_found cb)
{
    // Prepare socket
    int r;
    int sock;
    struct timeval timeout = {
        .tv_sec = 4,
    };

    sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return -errno;
    }

    r = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (r < 0) {
        r = -errno;
        goto end;
    }

    // Send request
    r = coap_sd_send_req(name, type, sock);
    if (r < 0) {
        goto end;
    }

    // Get responses and execute callback for each valid one
    r = coap_sd_receive_rsp(sock, cb, name, type);
end:
    close(sock);

    return r;
}
