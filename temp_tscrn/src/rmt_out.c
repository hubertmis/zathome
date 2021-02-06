/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rmt_out.h"

#include <kernel.h>
#include <net/coap.h>
#include <net/socket.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>

#include "coap.h"
#include "prov.h"

#define RMT_OUT_LOC DATA_LOC_LOCAL
#define OUT_MAX 256
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

#define OUT_THREAD_STACK_SIZE 2048
#define OUT_THREAD_PRIO       0
static void out_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(out_thread_id, OUT_THREAD_STACK_SIZE,
                out_thread_process, NULL, NULL, NULL,
                OUT_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

#define SD_THREAD_STACK_SIZE 2048
#define SD_THREAD_PRIO       0
static void sd_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(sd_thread_id, SD_THREAD_STACK_SIZE,
                sd_thread_process, NULL, NULL, NULL,
                SD_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

static struct in6_addr discovered_addr;
static int sd_missed;

K_SEM_DEFINE(to_sem, 0, 1);

#define TO_THREAD_STACK_SIZE 1024
#define TO_THREAD_PRIO       0
static void to_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(to_thread_id, TO_THREAD_STACK_SIZE,
                to_thread_process, NULL, NULL, NULL,
                TO_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

static struct in6_addr discovered_addr;
static int sd_missed;

void output_found(const struct sockaddr *src_addr, const socklen_t *addrlen,
                  const char *name, const char *type)
{
    const struct sockaddr_in6 *addr_in6;
    const char *expected_name = prov_get_loc_output_label();
    if (strcmp(name, expected_name) != 0) {
        return;
    }
    if (strcmp(type, OUT_TYPE) != 0) {
        return;
    }

    if (src_addr->sa_family != AF_INET6) {
        return;
    }

    addr_in6 = (const struct sockaddr_in6 *)src_addr;

    // Restart timeout timer
    k_sem_give(&to_sem);

    // TODO: Mutex when using discovered_addr
    memcpy(&discovered_addr, &addr_in6->sin6_addr, sizeof(discovered_addr));
    sd_missed = 0;
}

static void sd_thread_process(void *a1, void *a2, void *a3)
{
    (void)a1;
    (void)a2;
    (void)a3;

    bool out_thread_started = false;

    // Start timeout timer
    k_thread_start(to_thread_id);

    sd_missed = 0;

    while (1) {
        int wait_ms;
        const char *name = prov_get_loc_output_label();

        if (!name || !strlen(name)) {
            sd_missed = 0;
            k_sleep(K_MSEC(MIN_SD_INTERVAL));
            continue;
        }

        sd_missed++; // Increment up front. coap_sd_start would eventually clear it.
        (void)coap_sd_start(name, OUT_TYPE, output_found);

        wait_ms = sd_missed > 0 ? sd_missed * MIN_SD_INTERVAL : MAX_SD_INTERVAL;
        if (wait_ms > MAX_SD_INTERVAL) {
            wait_ms = MAX_SD_INTERVAL;
        }

        // Start out thread after initial SD is finished
        if (!out_thread_started) {
            k_thread_start(out_thread_id);
            out_thread_started = true;
        }

        k_sleep(K_MSEC(wait_ms));
    }
}

static void to_thread_process(void *a1, void *a2, void *a3)
{
    (void)a1;
    (void)a2;
    (void)a3;

    while (1) {
        if (k_sem_take(&to_sem, K_MSEC(TO_INTERVAL)) != 0) {
            // Timeout
            // TODO: Mutex when using discovered_addr
            memcpy(&discovered_addr, net_ipv6_unspecified_address(), sizeof(discovered_addr));
        } else {
            // Timeout timer restarted. Do nothing.
        }
    }
}

static int prepare_req_payload(uint8_t *payload, size_t len, int val)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 1) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, OUT_KEY, strlen(OUT_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, val) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
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

        // TODO: Mutex when using discovered_addr
        memcpy(addr, &discovered_addr, sizeof(*addr));

        if (!net_ipv6_is_addr_unspecified(addr))
        {
            data_dispatcher_get(DATA_OUTPUT, RMT_OUT_LOC, &out_data);

            out_val = out_data->output * OUT_MAX / UINT16_MAX;

            do {
                send_req(sock, &rmt_addr, out_val);

                r = rcv_rsp(sock);
                cnt++;
            } while ((r != 0) && (cnt < 5));
        }

        k_sleep(K_MSEC(OUT_INTERVAL));
    }

end:
    close(sock);
}

void rmt_out_init(void)
{
    memcpy(&discovered_addr, net_ipv6_unspecified_address(), sizeof(discovered_addr));

    k_thread_start(sd_thread_id);
}
