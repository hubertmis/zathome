/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "light_conn.h"

#include <kernel.h>
#include <net/coap.h>
#include <net/socket.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>

#include "data_dispatcher.h"

#include <continuous_sd.h>

#define LIGHT_TYPE "rgbw"

#define TO_INTERVAL (1000UL * 60UL * 31UL)

#define STATE_INTERVAL (1000UL * 6UL)

#define COAP_PORT 5683
#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 64
#define COAP_CONTENT_FORMAT_CBOR 60

#define LIGHT_R_KEY "r"
#define LIGHT_G_KEY "g"
#define LIGHT_B_KEY "b"
#define LIGHT_W_KEY "w"
#define DURATION_KEY "d"

K_SEM_DEFINE(light_out_sem, 0, 1);
K_SEM_DEFINE(light_state_sem, 0, 1);

#define OUT_THREAD_STACK_SIZE 2048
#define OUT_THREAD_PRIO       0
static void out_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(light_out_thread_id, OUT_THREAD_STACK_SIZE,
                out_thread_process, NULL, NULL, NULL,
                OUT_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

#define STATE_THREAD_STACK_SIZE 2048
#define STATE_THREAD_PRIO       0
static void state_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(light_state_thread_id, STATE_THREAD_STACK_SIZE,
                state_thread_process, NULL, NULL, NULL,
                STATE_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

static const char *names[LIGHT_CONN_ITEM_NUM] = { "bbl", "bwl", "ll", "drl" };

static int active_item = -1;
static data_light_t light_out_val;

static int prepare_req_payload(uint8_t *payload, size_t len, data_light_t *data)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 5) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, LIGHT_R_KEY, strlen(LIGHT_R_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_uint(&map, data->r) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, LIGHT_G_KEY, strlen(LIGHT_G_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_uint(&map, data->g) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, LIGHT_B_KEY, strlen(LIGHT_B_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_uint(&map, data->b) != CborNoError) return -EINVAL;
    
    if (cbor_encode_text_string(&map, LIGHT_W_KEY, strlen(LIGHT_W_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_uint(&map, data->w) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, DURATION_KEY, strlen(DURATION_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_uint(&map, 250) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int send_req(int sock, struct sockaddr_in6 *addr, const char *name, data_light_t *light_data)
{
    int r;
    struct coap_packet cpkt;
    uint8_t *data;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];

    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
    if (!data) {
        return -ENOMEM;
    }

    r = coap_packet_init(&cpkt, data, MAX_COAP_MSG_LEN,
                 1, COAP_TYPE_CON, 4, coap_next_token(),
                 COAP_METHOD_POST, coap_next_id());
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_option(&cpkt, COAP_OPTION_URI_PATH, name, strlen(name));
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

    r = prepare_req_payload(payload, MAX_COAP_PAYLOAD_LEN, light_data);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload(&cpkt, payload, r);
    if (r < 0) {
        goto end;
    }

    r = sendto(sock, cpkt.data, cpkt.offset, 0, (struct sockaddr *)addr, sizeof(*addr));
    if (r < 0) {
        r = -errno;
        goto end;
    }

end:
    k_free(data);

    return r;
}

static int rcv_rsp(int sock)
{
    int r;
    struct sockaddr addr;
    socklen_t addr_len;
    uint8_t response[MAX_COAP_MSG_LEN];

    r = recvfrom(sock, response, sizeof(response), 0, &addr, &addr_len);
    // TODO: Parse response and verify if it is ACK with correct token, msg_id

    if (r < 0) {
        return -errno;
    }

    return 0;
}
static void out_thread_process(void *a1, void *a2, void *a3)
{
    (void)a1;
    (void)a2;
    (void)a3;

    // Prepare socket
    int r;
    int sock;
    struct timeval timeout = {
        .tv_sec = 4,
    };
    struct sockaddr_in6 rmt_addr = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(COAP_PORT),
    };
    struct in6_addr *addr = &rmt_addr.sin6_addr;

    sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return;
    }

    r = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (r < 0) {
        r = -errno;
        goto end;
    }

    while (1) {
        int cnt = 0;

        k_sem_take(&light_out_sem, K_FOREVER);

        // TODO: Mutex when using discovered_addr or item or data?
	int item = active_item;
        data_light_t data = light_out_val;
	if (item < 0 || item >= LIGHT_CONN_ITEM_NUM) continue;
	r = continuous_sd_get_addr(names[item], LIGHT_TYPE, addr);
	if (r) continue; // TODO: Try faster?

        if (!net_ipv6_is_addr_unspecified(addr))
        {
            do {
                send_req(sock, &rmt_addr, names[item], &data);

                r = rcv_rsp(sock);
                cnt++;
            } while ((r != 0) && (cnt < 5));
        }
    }

end:
    close(sock);
}

static int send_state_req(int sock, struct sockaddr_in6 *addr, const char *name)
{
    int r;
    struct coap_packet cpkt;
    uint8_t *data;

    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
    if (!data) {
        return -ENOMEM;
    }

    r = coap_packet_init(&cpkt, data, MAX_COAP_MSG_LEN,
                 1, COAP_TYPE_CON, 4, coap_next_token(),
                 COAP_METHOD_GET, coap_next_id());
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_option(&cpkt, COAP_OPTION_URI_PATH, name, strlen(name));
    if (r < 0) {
        goto end;
    }

    r = sendto(sock, cpkt.data, cpkt.offset, 0, (struct sockaddr *)addr, sizeof(*addr));
    if (r < 0) {
        r = -errno;
        goto end;
    }

end:
    k_free(data);

    return r;
}

static int parse_color_key(const CborValue *top_map, const char *key, uint8_t *result)
{
    CborError cbor_error;
    CborValue map_val;
    uint64_t val;

    cbor_error = cbor_value_map_find_value(top_map, key, &map_val);
    if ((cbor_error != CborNoError) || !cbor_value_is_unsigned_integer(&map_val)) {
        return -EINVAL;
    }

    cbor_error = cbor_value_get_uint64(&map_val, &val);
    if (cbor_error != CborNoError) {
        return -EINVAL;
    }

    if (val > UINT8_MAX) {
        return -EINVAL;
    }

    *result = val;
    return 0;
}

static int rcv_state_rsp(int sock)
{
    int r;
    struct sockaddr addr;
    socklen_t addr_len = sizeof(addr);
    uint8_t response[MAX_COAP_MSG_LEN];

    r = recvfrom(sock, response, sizeof(response), 0, &addr, &addr_len);
    // TODO: Parse response and verify if it is ACK with correct token, msg_id

    if (r < 0) {
        return -errno;
    }

    struct coap_packet rsp;
    uint8_t coap_type;
    struct coap_option option;
    const uint8_t *payload;
    uint16_t payload_len;

    r = coap_packet_parse(&rsp, response, r, NULL, 0);
    if (r < 0) {
        return r;
    }

    coap_type = coap_header_get_type(&rsp);

    if (coap_type != COAP_TYPE_ACK) {
        return -EINVAL;
    }

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
    CborValue top_map;
    struct cbor_buf_reader reader;

    cbor_buf_reader_init(&reader, payload, payload_len);

    cbor_error = cbor_parser_init(&reader.r, 0, &parser, &top_map);
    if (cbor_error != CborNoError) {
        return -EINVAL;
    }

    if (!cbor_value_is_map(&top_map)) {
        return -EINVAL;
    }
    
    data_dispatcher_publish_t data = {
        .type = DATA_LIGHT_CURR,
    };

    r = parse_color_key(&top_map, LIGHT_R_KEY, &data.light.r);
    if (r < 0) return r;
    r = parse_color_key(&top_map, LIGHT_G_KEY, &data.light.g);
    if (r < 0) return r;
    r = parse_color_key(&top_map, LIGHT_B_KEY, &data.light.b);
    if (r < 0) return r;
    r = parse_color_key(&top_map, LIGHT_W_KEY, &data.light.w);
    if (r < 0) return r;

    data_dispatcher_publish(&data);

    return 0;
}

static void state_thread_process(void *a1, void *a2, void *a3)
{
    (void)a1;
    (void)a2;
    (void)a3;

    // Prepare socket
    int r;
    int sock;
    struct timeval timeout = {
        .tv_sec = 4,
    };
    struct sockaddr_in6 rmt_addr = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(COAP_PORT),
    };
    struct in6_addr *addr = &rmt_addr.sin6_addr;

    sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return;
    }

    r = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (r < 0) {
        r = -errno;
        goto end;
    }

    while (1) {
        int cnt = 0;

        k_sem_take(&light_state_sem, K_MSEC(STATE_INTERVAL));

	int item = active_item;
	if (item < 0 || item >= LIGHT_CONN_ITEM_NUM) continue;
	r = continuous_sd_get_addr(names[item], LIGHT_TYPE, addr);
	if (r) continue; // TODO: Retry faster?

        if (!net_ipv6_is_addr_unspecified(addr))
        {
            do {
                send_state_req(sock, &rmt_addr, names[item]);

                r = rcv_state_rsp(sock);
                cnt++;
            } while ((r != 0) && (cnt < 5));
        }
    }

end:
    close(sock);
}

static void light_requested(const data_dispatcher_publish_t *data) {
    // TODO: mutex over light out val?
    light_out_val = data->light;
    k_sem_give(&light_out_sem);
}

static data_dispatcher_subscribe_t light_req_sbscr = {
    .callback = light_requested,
};

void light_conn_init(void)
{
    data_dispatcher_subscribe(DATA_LIGHT_REQ, &light_req_sbscr);

    // TODO: Move it inside SD thread?
    k_sleep(K_SECONDS(3));

    for (int i = 0; i < ARRAY_SIZE(names); i++) {
	    continuous_sd_register(names[i], LIGHT_TYPE, true);
	    k_sleep(K_SECONDS(2));
    }

    k_thread_start(light_state_thread_id);
    k_thread_start(light_out_thread_id);
}

void light_conn_enable_polling(int item)
{
	if (item < 0 || item >= LIGHT_CONN_ITEM_NUM) return;

	active_item = item;
	k_sem_give(&light_state_sem);
}

void light_conn_disable_polling(void)
{
	active_item = -1;
}
