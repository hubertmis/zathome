/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "vent_conn.h"

#include <kernel.h>
#include <net/coap.h>
#include <net/socket.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>

#include "coap.h"

#define VENT_NAME "ap"
#define VENT_TYPE "airpack"

#define SM_KEY        "sm"
#define SM_VAL_AIRING "a"
#define SM_VAL_NONE   "a"

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

K_THREAD_DEFINE(vent_out_thread_id, OUT_THREAD_STACK_SIZE,
                out_thread_process, NULL, NULL, NULL,
                OUT_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

#define SD_THREAD_STACK_SIZE 2048
#define SD_THREAD_PRIO       0
static void sd_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(vent_sd_thread_id, SD_THREAD_STACK_SIZE,
                sd_thread_process, NULL, NULL, NULL,
                SD_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

static struct in6_addr discovered_addr;
static int sd_missed;

K_SEM_DEFINE(vent_to_sem, 0, 1);

#define TO_THREAD_STACK_SIZE 1024
#define TO_THREAD_PRIO       0
static void to_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(vent_to_thread_id, TO_THREAD_STACK_SIZE,
                to_thread_process, NULL, NULL, NULL,
                TO_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

static struct in6_addr discovered_addr;
static int sd_missed;

void airpack_found(const struct sockaddr *src_addr, const socklen_t *addrlen,
                  const char *name, const char *type)
{
    const struct sockaddr_in6 *addr_in6;
    if (strcmp(name, VENT_NAME) != 0) {
        return;
    }
    if (strcmp(type, VENT_TYPE) != 0) {
        return;
    }

    if (src_addr->sa_family != AF_INET6) {
        return;
    }

    addr_in6 = (const struct sockaddr_in6 *)src_addr;

    // Restart timeout timer
    k_sem_give(&vent_to_sem);

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
    k_thread_start(vent_to_thread_id);

    sd_missed = 0;

    while (1) {
        int wait_ms;

        sd_missed++; // Increment up front. coap_sd_start would eventually clear it.
        (void)coap_sd_start(VENT_NAME, VENT_TYPE, airpack_found);

        wait_ms = sd_missed > 0 ? sd_missed * MIN_SD_INTERVAL : MAX_SD_INTERVAL;
        if (wait_ms > MAX_SD_INTERVAL) {
            wait_ms = MAX_SD_INTERVAL;
        }

        // Start out thread after initial SD is finished
        if (!out_thread_started) {
            //TODO: Do something about out thread
            //k_thread_start(vent_out_thread_id);
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
        if (k_sem_take(&vent_to_sem, K_MSEC(TO_INTERVAL)) != 0) {
            // Timeout
            // TODO: Mutex when using discovered_addr
            memcpy(&discovered_addr, net_ipv6_unspecified_address(), sizeof(discovered_addr));
            // TODO: Notify unavailability for display
        } else {
            // Timeout timer restarted. Do nothing.
        }
    }
}

static int prepare_req_payload(uint8_t *payload, size_t len, char *sm_val)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder arr;
    CborEncoder map;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_array(&ce, &arr, 1) != CborNoError) return -EINVAL;
    if (cbor_encoder_create_map(&arr, &map, 1) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, SM_KEY, strlen(SM_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_text_string(&map, sm_val, strlen(sm_val)) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&arr, &map) != CborNoError) return -EINVAL;
    if (cbor_encoder_close_container(&ce, &arr) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
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
                 COAP_METHOD_PUT, coap_next_id());
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

#if 0
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
#endif

    while (1) {
#if 0
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
#endif
    }

#if 0
end:
    close(sock);
#endif
}

void vent_conn_init(void)
{
    memcpy(&discovered_addr, net_ipv6_unspecified_address(), sizeof(discovered_addr));

    // TODO: Move it inside SD thread?
    k_sleep(K_SECONDS(15));

    k_thread_start(vent_sd_thread_id);
}
