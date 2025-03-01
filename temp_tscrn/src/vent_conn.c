/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "vent_conn.h"

#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/kernel.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>

#include "coap.h"
#include "data_dispatcher.h"

#include <continuous_sd.h>

#define VENT_NAME "ap"
#define VENT_TYPE "airpack"

#define SM_KEY        "sm"
#define SM_VAL_NONE   "n"
#define SM_VAL_AIRING "a"
#define SM_MAX_LEN    4

#define TO_INTERVAL (1000UL * 60UL * 31UL)

#define STATE_INTERVAL (1000UL * 60UL)

#define COAP_PORT 5683
#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 64
#define COAP_CONTENT_FORMAT_CBOR 60

static char *vent_out_val;
K_SEM_DEFINE(vent_out_sem, 0, 1);
K_SEM_DEFINE(vent_state_sem, 0, 1);

#define OUT_THREAD_STACK_SIZE 2048
#define OUT_THREAD_PRIO       1
static void out_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(vent_out_thread_id, OUT_THREAD_STACK_SIZE,
                out_thread_process, NULL, NULL, NULL,
                OUT_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

#define STATE_THREAD_STACK_SIZE 2048
#define STATE_THREAD_PRIO       1
static void state_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(vent_state_thread_id, STATE_THREAD_STACK_SIZE,
                state_thread_process, NULL, NULL, NULL,
                STATE_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

static int prepare_req_payload(uint8_t *payload, size_t len, char *sm_val)
{
    ZCBOR_STATE_E(ce, 2, payload, len, 1);

    if (!zcbor_list_start_encode(ce, 1)) return -EINVAL;
    if (!zcbor_map_start_encode(ce, 1)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, SM_KEY)) return -EINVAL;
    if (!zcbor_tstr_put_term(ce, sm_val, SM_MAX_LEN)) return -EINVAL;

    if (!zcbor_map_end_encode(ce, 1)) return -EINVAL;
    if (!zcbor_list_end_encode(ce, 1)) return -EINVAL;

    return (size_t)(ce->payload - payload);
}

static int send_req(int sock, struct sockaddr_in6 *addr, char *sm_val)
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

    r = coap_packet_append_option(&cpkt, COAP_OPTION_URI_PATH, VENT_NAME, strlen(VENT_NAME));
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

    r = prepare_req_payload(payload, MAX_COAP_PAYLOAD_LEN, sm_val);
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

        k_sem_take(&vent_out_sem, K_FOREVER);

        r = continuous_sd_get_addr(VENT_NAME, VENT_TYPE, addr);

        if (!r && !net_ipv6_is_addr_unspecified(addr))
        {
            do {
                send_req(sock, &rmt_addr, vent_out_val);

                r = rcv_rsp(sock);
                cnt++;
            } while ((r != 0) && (cnt < 5));
        }
    }

end:
    close(sock);
}

static int send_state_req(int sock, struct sockaddr_in6 *addr)
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

    r = coap_packet_append_option(&cpkt, COAP_OPTION_URI_PATH, VENT_NAME, strlen(VENT_NAME));
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

    struct zcbor_string sm_text;

    ZCBOR_STATE_D(cd, 2, payload, payload_len, 1, 0);

    if (!zcbor_unordered_map_start_decode(cd)) return -EINVAL;

    if (!zcbor_search_key_tstr_lit(cd, SM_KEY)) return -EINVAL;
    if (!zcbor_tstr_decode(cd, &sm_text)) return -EINVAL;

    if (!zcbor_list_map_end_force_decode(cd)) return -EINVAL;

    data_dispatcher_publish_t data = {
        .type = DATA_VENT_CURR,
    };

    if (strncmp(sm_text.value, SM_VAL_NONE, sm_text.len) == 0) {
        data.vent_mode = VENT_SM_NONE;
    } else if (strncmp(sm_text.value, SM_VAL_AIRING, sm_text.len) == 0) {
        data.vent_mode = VENT_SM_AIRING;
    } else {
        return -EINVAL;
    }

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

        k_sem_take(&vent_state_sem, K_MSEC(STATE_INTERVAL));

        r = continuous_sd_get_addr(VENT_NAME, VENT_TYPE, addr);

        if (!r && !net_ipv6_is_addr_unspecified(addr))
        {
            do {
                send_state_req(sock, &rmt_addr);

                r = rcv_state_rsp(sock);
                cnt++;
            } while ((r != 0) && (cnt < 5));
        }
    }

end:
    close(sock);
}

static void vent_requested(const data_dispatcher_publish_t *data) {
    switch (data->vent_mode) {
        case VENT_SM_UNAVAILABLE:
        case VENT_SM_NONE:
            vent_out_val = SM_VAL_NONE;
            break;

        case VENT_SM_AIRING:
            vent_out_val = SM_VAL_AIRING;
            break;
    }

    k_sem_give(&vent_out_sem);
}

static data_dispatcher_subscribe_t vent_req_sbscr = {
    .callback = vent_requested,
};

void vent_conn_init(void)
{
    data_dispatcher_subscribe(DATA_VENT_REQ, &vent_req_sbscr);

    // TODO: Move it inside SD thread?
    k_sleep(K_SECONDS(15));
    continuous_sd_register(VENT_NAME, VENT_TYPE, false);

    k_sleep(K_SECONDS(1));
    k_thread_start(vent_state_thread_id);
    k_thread_start(vent_out_thread_id);
}
