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
#include <continuous_sd.h>
#include "data_dispatcher.h"
#include "prov.h"

#include <net/socket.h>
#include <net/coap.h>
#include <net/fota_download.h>
#include <net/tls_credentials.h>
#include <random/rand32.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>
#include <zephyr.h>

#define COAP_PORT 5683
#define COAPS_PORT 5684
#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 64

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

static int send_coap_reply(int sock,
               struct coap_packet *cpkt,
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

static int send_ack(int sock, const struct sockaddr *addr, socklen_t addr_len,
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

    r = send_coap_reply(sock, &response, addr, addr_len);

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

    r = prepare_temp_payload(payload, MAX_COAP_PAYLOAD_LEN, loc);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload(&response, payload, r);
    if (r < 0) {
        goto end;
    }

    r = send_coap_reply(sock, &response, addr, addr_len);

end:
    k_free(data);

    return r;
}

static int temp_post(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len,
             data_loc_t loc)
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
        send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (coap_option_value_to_int(&option) != COAP_CONTENT_FORMAT_CBOR) {
        send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT, token, tkl);
        return -EINVAL;
    }

    payload = coap_packet_get_payload(request, &payload_len);
    if (!payload) {
        send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    CborError cbor_error;
    CborParser parser;
    CborValue value;
    struct cbor_buf_reader reader;

    cbor_buf_reader_init(&reader, payload, payload_len);

    cbor_error = cbor_parser_init(&reader.r, 0, &parser, &value);
    if (cbor_error != CborNoError) {
        send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (!cbor_value_is_map(&value)) {
        send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    // Handle temperature setting
    CborValue sett_cbor_el;

    cbor_error = cbor_value_map_find_value(&value, SETT_KEY, &sett_cbor_el);
    if ((cbor_error == CborNoError) && cbor_value_is_valid(&sett_cbor_el)) {
        int temp_val;
        r = cbor_decode_dec_frac_num(&sett_cbor_el, -1, &temp_val);

        if (r != 0) {
            send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
            return r;
        }

        data_dispatcher_publish_t sett = {
            .loc = loc,
            .type = DATA_TEMP_SETTING,
            .temp_setting = temp_val,
        };
        data_dispatcher_publish(&sett);

        rsp_code = COAP_RESPONSE_CODE_CHANGED;
    }

    // Handle controller
    CborValue cnt_cbor_el;

    cbor_error = cbor_value_map_find_value(&value, CNT_KEY, &cnt_cbor_el);
    if ((cbor_error == CborNoError) && cbor_value_is_valid(&cnt_cbor_el)) {
        if (!cbor_value_is_map(&cnt_cbor_el)) {
            send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
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

    r = send_ack(sock, addr, addr_len, id, rsp_code, token, tkl);
    return r;
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
        send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (coap_option_value_to_int(&option) != COAP_CONTENT_FORMAT_CBOR) {
        send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT, token, tkl);
        return -EINVAL;
    }

    payload = coap_packet_get_payload(request, &payload_len);
    if (!payload) {
        send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
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
        send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (!cbor_value_is_map(&value)) {
        send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
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

    r = send_ack(sock, addr, addr_len, id, rsp_code, token, tkl);
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

    r = send_coap_reply(sock, &response, addr, addr_len);

end:
    k_free(data);

    return r;
}

static int prepare_cont_sd_dbg_payload(uint8_t *payload, size_t len)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;

    int state;
    int64_t target_time;
    int64_t now = k_uptime_get();
    const char *name;
    const char *type;
    int sd_missed;
    continuous_sd_debug(&state, &target_time, &name, &type, &sd_missed);

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 6) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, "n", strlen("n")) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&map, name, strlen(name)) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, "t", strlen("t")) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&map, type, strlen(type)) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, "s", strlen("s")) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, state) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, "nt", strlen("nt")) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, now) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, "tt", strlen("tt")) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, target_time) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, "sm", strlen("sm")) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, sd_missed) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int cont_sd_dbg_get(struct coap_resource *resource,
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

    r = prepare_cont_sd_dbg_payload(payload, MAX_COAP_PAYLOAD_LEN);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload(&response, payload, r);
    if (r < 0) {
        goto end;
    }

    r = send_coap_reply(sock, &response, addr, addr_len);

end:
    k_free(data);

    return r;
}

static struct coap_resource * rsrcs_get(int sock)
{
    static const char * const fota_path [] = {"fota_req", NULL};
    static const char * const sd_path [] = {"sd", NULL};
    static const char * const prov_path[] = {"prov", NULL};
    static const char * const cont_sd_dbg_path[] = {"cont_sd", NULL};
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
	{ .get = cont_sd_dbg_get,
	  .path = cont_sd_dbg_path,
	},
	{ .get = temp_remote_get,
	  .post = temp_remote_post,
          .path = rsrc0_path,
	},
	{ .get = temp_local_get,
	  .post = temp_local_post,
          .path = rsrc1_path,
	},
        { .path = NULL } // Array terminator
    };

    rsrc0_path[0] = prov_get_rsrc_label(DATA_LOC_REMOTE);
    rsrc1_path[0] = prov_get_rsrc_label(DATA_LOC_LOCAL);

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
