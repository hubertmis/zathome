/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap_req.h"

#include <net/coap.h>

#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>

#define COAP_PORT 5683
#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 64

#define PRESET_KEY "p"

static int prepare_req_payload(uint8_t *payload, size_t len, int val)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, 1) != CborNoError) return -EINVAL;

    if (cbor_encode_text_string(&map, PRESET_KEY, strlen(PRESET_KEY)) != CborNoError) return -EINVAL;
    if (cbor_encode_int(&map, val) != CborNoError) return -EINVAL;

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}


static int send_req(int sock, struct sockaddr_in6 *addr, const char *rsrc, int out_val)
{
    int r;
    struct coap_packet cpkt;
    uint8_t *data;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];

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
                 COAP_METHOD_POST, coap_next_id());
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_option(&cpkt, COAP_OPTION_URI_PATH, rsrc, strlen(rsrc));
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

int coap_req_preset(struct in6_addr *addr, const char *rsrc, int preset_id)
{
    int r = 0;
    int sock;
    struct timeval timeout = {
        .tv_sec = 4,
    };
    struct sockaddr_in6 rmt_addr = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(COAP_PORT),
    };

    sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return sock;
    }

    r = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (r < 0) {
        r = -errno;
        goto end;
    }

    memcpy(&rmt_addr.sin6_addr, addr, sizeof(rmt_addr.sin6_addr));

    if (net_ipv6_is_addr_unspecified(&rmt_addr.sin6_addr)) {
        r = -EINVAL;
    }

    send_req(sock, &rmt_addr, rsrc, preset_id);
    r = rcv_rsp(sock);

end:
    close(sock);
    return r;
}
