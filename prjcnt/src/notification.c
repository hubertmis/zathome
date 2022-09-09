/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "notification.h"

#include <kernel.h>
#include <net/coap.h>
#include <net/socket.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>

#include <continuous_sd.h>

#define COAP_PORT 5683
#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 64

#define PRJ_ENABLED_URI_PATH "prj"
#define PRJ_ENABLED_KEY "p"

#define NTF_INTERVAL (15 * 1000)
#define NTF_TARGETS_NUM 4
static const char *ntf_targets[NTF_TARGETS_NUM];

K_SEM_DEFINE(ntf_out_sem, 0, 1);

#define OUT_THREAD_STACK_SIZE 2048
#define OUT_THREAD_PRIO       0
static void out_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(ntf_out_thread_id, OUT_THREAD_STACK_SIZE,
                out_thread_process, NULL, NULL, NULL,
                OUT_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

static bool prj_enabled = true;

static int prepare_req_payload(uint8_t *payload, size_t len, bool enabled)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 1) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, PRJ_ENABLED_KEY, strlen(PRJ_ENABLED_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_boolean(&map, enabled) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int send_req(int sock, struct sockaddr_in6 *addr, const char *rsrc, bool prj_enabled)
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
                 1, COAP_TYPE_NON_CON, 4, coap_next_token(),
                 COAP_METHOD_POST, coap_next_id());
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_option(&cpkt, COAP_OPTION_URI_PATH, rsrc, strlen(rsrc));
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_option(&cpkt, COAP_OPTION_URI_PATH, PRJ_ENABLED_URI_PATH, strlen(PRJ_ENABLED_URI_PATH));
    if (r < 0) {
        goto end;
    }

    r = coap_append_option_int(&cpkt, COAP_OPTION_CONTENT_FORMAT,
            COAP_CONTENT_FORMAT_APP_CBOR);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload_marker(&cpkt);
    if (r < 0) {
        goto end;
    }

    r = prepare_req_payload(payload, MAX_COAP_PAYLOAD_LEN, prj_enabled);
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

static void out_thread_process(void *a1, void *a2, void *a3)
{
    (void)a1;
    (void)a2;
    (void)a3;

    // Prepare socket
    int r;
    int sock;
    struct sockaddr_in6 rmt_addr = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(COAP_PORT),
    };
    struct in6_addr *addr = &rmt_addr.sin6_addr;

    sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return;
    }

    while (1) {
        k_sem_take(&ntf_out_sem, K_MSEC(NTF_INTERVAL));

        // TODO: Mutex when using discovered_addr?
        for (int i = 0; i < NTF_TARGETS_NUM; i++)
        {
            const char *out_label = ntf_targets[i];
            if (out_label == NULL) continue;
        
            r = continuous_sd_get_addr(out_label, NULL, addr);
            if (r) continue; // TODO: Try faster?
        
            if (!net_ipv6_is_addr_unspecified(addr))
            {
       	        send_req(sock, &rmt_addr, out_label, prj_enabled);
            }
        }
    }

    close(sock);
}

void notification_init(void)
{
	for (int i = 0; i < NTF_TARGETS_NUM; i++)
	{
		ntf_targets[i] = NULL;
	}

	k_thread_start(ntf_out_thread_id);
}

void notification_reset_targets(void)
{
	for (int i = 0; i < NTF_TARGETS_NUM; i++)
	{
		ntf_targets[i] = NULL;
	}

	continuous_sd_unregister_all();
}

int notification_add_target(const char *name)
{
	int r;

	for (int i = 0; i < NTF_TARGETS_NUM; i++)
	{
		if (ntf_targets[i] != NULL) continue;
		r = continuous_sd_register(name, NULL, true);
		
		if (!r) {
			ntf_targets[i] = name;
		}

		return r;
	}

	return -ENOMEM;
}

void notification_set_prj_state(bool enabled)
{
	prj_enabled = enabled;
	k_sem_give(&ntf_out_sem);
}
