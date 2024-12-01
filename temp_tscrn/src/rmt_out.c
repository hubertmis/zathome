/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rmt_out.h"

#include <zephyr/kernel.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zcbor_encode.h>

#include "coap.h"
#include "prov.h"

#include <continuous_sd.h>

#define RMT_OUT_LOC DATA_LOC_LOCAL
#define OUT_MAX 256UL
#define OUT_KEY "val"
#define OUT_TYPE "shcnt"

#define OUT_INTERVAL (1000UL * 60UL * 2UL)

#define MIN_SD_INTERVAL (1000UL * 10UL)
#define MAX_SD_INTERVAL (1000UL * 60UL * 10UL)

#define TO_INTERVAL (1000UL * 60UL * 31UL)

#define COAP_PORT 5683
#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 64
#define COAP_CONTENT_FORMAT_CBOR 60

static char rsrc_name[PROV_LBL_MAX_LEN];

#define OUT_THREAD_STACK_SIZE 2048
#define OUT_THREAD_PRIO       0
static void out_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(out_thread_id, OUT_THREAD_STACK_SIZE,
                out_thread_process, NULL, NULL, NULL,
                OUT_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

K_SEM_DEFINE(to_sem, 0, 1);

static int prepare_req_payload(uint8_t *payload, size_t len, int val)
{
	ZCBOR_STATE_E(ce, 1, payload, len, 1);

	if (!zcbor_map_start_encode(ce, 1)) return -EINVAL;

	if (!zcbor_tstr_encode_ptr(ce, OUT_KEY, strlen(OUT_KEY))) return -EINVAL;
	if (!zcbor_int32_put(ce, val)) return -EINVAL;

	if (!zcbor_map_end_encode(ce, 1)) return -EINVAL;

	return (size_t)(ce->payload - payload);
}

static int send_req(int sock, struct sockaddr_in6 *addr, int out_val)
{
    int r;
    struct coap_packet cpkt;
    uint8_t *data;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    const char *rsrc = prov_get_loc_output_label();

    if (!rsrc || !strlen(rsrc)) {
        // TODO: Here is a race condition. Actually resource may be removed before
        // packet is created
        return -EINVAL;
    }

    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
    if (!data) {
        return -ENOMEM;
    }

    r = coap_packet_init(&cpkt, data, MAX_COAP_MSG_LEN,
                 1, COAP_TYPE_CON, 4, coap_next_token(),
                 COAP_METHOD_PUT, coap_next_id());
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_option(&cpkt, COAP_OPTION_URI_PATH, rsrc, strlen(rsrc));
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

    r = prepare_req_payload(payload, MAX_COAP_PAYLOAD_LEN, out_val);
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
        const data_dispatcher_publish_t *out_data;
        int out_val;

        k_sleep(K_MSEC(OUT_INTERVAL));

        const char *expected_name = prov_get_loc_output_label();
	r = continuous_sd_get_addr(expected_name, OUT_TYPE, addr);
	if (r == -ENOENT) {
            r = continuous_sd_unregister(rsrc_name, OUT_TYPE);
            strncpy(rsrc_name, expected_name, sizeof(rsrc_name));
            continuous_sd_register(rsrc_name, OUT_TYPE, true);
	    continue;
	}

        if (!r && !net_ipv6_is_addr_unspecified(addr))
        {
            data_dispatcher_get(DATA_OUTPUT, RMT_OUT_LOC, &out_data);

            out_val = out_data->output * OUT_MAX / UINT16_MAX;

            do {
                send_req(sock, &rmt_addr, out_val);

                r = rcv_rsp(sock);
                cnt++;
            } while ((r != 0) && (cnt < 5));
        }
    }

end:
    close(sock);
}

void rmt_out_init(void)
{
    k_sleep(K_SECONDS(10));

    const char *expected_name = prov_get_loc_output_label();
    if (expected_name && strlen(expected_name)) {
        strncpy(rsrc_name, expected_name, sizeof(rsrc_name));
        continuous_sd_register(rsrc_name, OUT_TYPE, true);
    }

    k_sleep(K_SECONDS(1));
    k_thread_start(out_thread_id);
}
