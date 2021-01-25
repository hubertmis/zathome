/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap.h"

#include <errno.h>
#include <stdint.h>

#include "data_dispatcher.h"

#include <zephyr.h>
#include <net/socket.h>
#include <net/coap.h>
#include <net/fota_download.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>

#define MY_COAP_PORT 5685
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

#define TAG_DECIMAL_FRACTION 4

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

static int prepare_temp_payload(uint8_t *payload, size_t len)
{
    const data_dispatcher_publish_t *meas, *sett, *output;
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;

    data_dispatcher_get(DATA_TEMP_MEASUREMENT, DATA_LOC_REMOTE, &meas);
    data_dispatcher_get(DATA_TEMP_SETTING, DATA_LOC_REMOTE, &sett);
    data_dispatcher_get(DATA_OUTPUT, DATA_LOC_REMOTE, &output);

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 3) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, MEAS_KEY, strlen(MEAS_KEY)) != CborNoError) return -EINVAL;
    if (encode_temp(&map, meas->temp_measurement) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, SETT_KEY, strlen(SETT_KEY)) != CborNoError) return -EINVAL;
    if (encode_temp(&map, sett->temp_setting) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, OUT_KEY, strlen(OUT_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, output->output) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int temp_handler(struct coap_resource *resource,
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

    r = prepare_temp_payload(payload, MAX_COAP_PAYLOAD_LEN);
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

    CborValue sett_cbor_el;

    cbor_error = cbor_value_map_find_value(&value, SETT_KEY, &sett_cbor_el);
    if (cbor_error != CborNoError) {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

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
            .loc = DATA_LOC_REMOTE,
            .type = DATA_TEMP_SETTING,
            .temp_setting = integer,
        };
        data_dispatcher_publish(&sett);
    } else {
        send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    r = send_ack(addr, addr_len, id, COAP_RESPONSE_CODE_CHANGED, token, tkl);
    return r;
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

static struct coap_resource resources[] = {
    { .get = temp_handler,
      .put = temp_put,
      .path = (const char * const []){"mbrfh", NULL},
    },
    { .get = fota_get,
      .put = fota_put,
      .path = (const char * const []){"fota_req", NULL},
    },
};

static void process_coap_request(uint8_t *data, uint16_t data_len,
                 struct sockaddr *client_addr,
                 socklen_t client_addr_len)
{
    struct coap_packet request;
    struct coap_option options[16] = { 0 };
    uint8_t opt_num = 16U;
    int r;

    r = coap_packet_parse(&request, data, data_len, options, opt_num);
    if (r < 0) {
        return;
    }

    r = coap_handle_request(&request, resources, options, opt_num,
                client_addr, client_addr_len);
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
