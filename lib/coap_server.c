/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap_server.h"

#include <errno.h>
#include <stdint.h>

#include <net/socket.h>
#include <net/coap.h>
#include <net/tls_credentials.h>
#include <tinycbor/cbor_buf_reader.h>
#include <zephyr.h>

#define COAP_PORT 5683
#define COAPS_PORT 5684
#define MAX_COAP_MSG_LEN 256

#ifndef COAPS_PSK
#error PSK for coaps is not defined
#endif
#define COAPS_PSK_ID "def"

static coap_rsrcs_getter_t rsrcs_get;

#define COAP_THREAD_STACK_SIZE 4096
#define COAP_THREAD_PRIO       0
static void coap_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(coap_thread_id, COAP_THREAD_STACK_SIZE,
                coap_thread_process, NULL, NULL, NULL,
                COAP_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

#define COAPS_THREAD_STACK_SIZE 8192
#define COAPS_THREAD_PRIO       0
static void coaps_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(coaps_thread_id, COAPS_THREAD_STACK_SIZE,
                coaps_thread_process, NULL, NULL, NULL,
                COAPS_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

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


static int start_coap_server(void)
{
    struct sockaddr_in6 addr6;
    int r;
    int sock;

    memset(&addr6, 0, sizeof(addr6));
    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons(COAP_PORT);

    sock = socket(addr6.sin6_family, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return -errno;
    }

    r = bind(sock, (struct sockaddr *)&addr6, sizeof(addr6));
    if (r < 0) {
        return -errno;
    }

    return sock;
}

static int start_coaps_server(void)
{
    struct sockaddr_in6 addr6;
    int r;
    int sock;
    const int PSK_TAG = 0;
    sec_tag_t sec_tag_opt[] = {
        PSK_TAG,
    };
    int role = TLS_DTLS_ROLE_SERVER;

    memset(&addr6, 0, sizeof(addr6));
    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons(COAPS_PORT);

    r = tls_credential_add(PSK_TAG, TLS_CREDENTIAL_PSK, COAPS_PSK, strlen(COAPS_PSK));
    if (r < 0) {
        return r;
    }

    r = tls_credential_add(PSK_TAG, TLS_CREDENTIAL_PSK_ID, COAPS_PSK_ID, strlen(COAPS_PSK_ID));
    if (r < 0) {
        return r;
    }

    sock = socket(addr6.sin6_family, SOCK_DGRAM, IPPROTO_DTLS_1_2);
    if (sock < 0) {
        return -errno;
    }

    r = setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tag_opt, sizeof(sec_tag_opt));
    if (sock < 0) {
        return -errno;
    }
    
    r = setsockopt(sock, SOL_TLS, TLS_DTLS_ROLE, &role, sizeof(&role));
    if (sock < 0) {
        return -errno;
    }

    r = bind(sock, (struct sockaddr *)&addr6, sizeof(addr6));
    if (r < 0) {
        return -errno;
    }

    return sock;
}

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

int coap_server_send_coap_reply(int sock,
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

int coap_server_send_ack(int sock, const struct sockaddr *addr, socklen_t addr_len,
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

    r = coap_server_send_coap_reply(sock, &response, addr, addr_len);

end:
    k_free(data);

    return r;
}

int coap_server_send_ack_with_payload(int sock, const struct sockaddr *addr, socklen_t addr_len,
                    uint16_t id, enum coap_response_code code, uint8_t *token, uint8_t tkl,
		    const uint8_t *payload, size_t payload_len)
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

    r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
            COAP_CONTENT_FORMAT_APP_CBOR);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload_marker(&response);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload(&response, payload, payload_len);
    if (r < 0) {
        goto end;
    }

    r = coap_server_send_coap_reply(sock, &response, addr, addr_len);

end:
    k_free(data);

    return r;
}

int coap_server_handle_simple_getter(int sock, const struct coap_resource *resource,
                    const struct coap_packet *request,
                    const struct sockaddr *addr, socklen_t addr_len,
                    const uint8_t *payload, size_t payload_len)
{
    uint16_t id;
    uint8_t  code;
    uint8_t  type;
    uint8_t  tkl;
    uint8_t  token[COAP_TOKEN_MAX_LEN];

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

    return coap_server_send_ack_with_payload(sock, addr, addr_len, id,
            COAP_RESPONSE_CODE_CONTENT, token, tkl, payload, payload_len);
}

int coap_server_handle_simple_setter(int sock, const struct coap_resource *resource,
                    const struct coap_packet *request,
                    const struct sockaddr *addr, socklen_t addr_len,
		    coap_server_cbor_map_handler_t cbor_map_handler, void *context)
{
    uint16_t id;
    uint8_t  code;
    uint8_t  type;
    uint8_t  tkl;
    uint8_t  token[COAP_TOKEN_MAX_LEN];
    int r = 0;
    struct coap_option option;
    const uint8_t *payload;
    uint16_t payload_len;
    enum coap_response_code rsp_code = 0;

    code = coap_header_get_code(request);
    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    // TODO: Should we accept NON as well?
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
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (coap_option_value_to_int(&option) != COAP_CONTENT_FORMAT_APP_CBOR) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT, token, tkl);
        return -EINVAL;
    }

    payload = coap_packet_get_payload(request, &payload_len);
    if (!payload) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    CborError cbor_error;
    CborParser parser;
    CborValue value;
    struct cbor_buf_reader reader;

    cbor_buf_reader_init(&reader, payload, payload_len);

    cbor_error = cbor_parser_init(&reader.r, 0, &parser, &value);
    if (cbor_error != CborNoError) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (!cbor_value_is_map(&value)) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    r = cbor_map_handler(&value, &rsp_code, context);
    if (rsp_code) {
        coap_server_send_ack(sock, addr, addr_len, id, rsp_code, token, tkl);
    }

    return r;
}

static void process_coap_request(int sock,
                                 uint8_t *data,
                                 uint16_t data_len,
                                 struct sockaddr *client_addr,
                                 socklen_t client_addr_len)
{
    struct coap_packet request;
    struct coap_option options[16] = { 0 };
    uint8_t opt_num = 16U;
    int r;

    struct coap_resource * resources = rsrcs_get(sock);

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

        coap_server_send_ack(sock,
                 client_addr,
                 client_addr_len,
                 id,
                 COAP_RESPONSE_CODE_NOT_FOUND,
                 token,
                 tkl);
    }
    if (r < 0) {
        return;
    }
}


static int process_client_request(int sock)
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

        process_coap_request(sock, request, received, &client_addr,
                     client_addr_len);
    } while (true);

    return 0;
}

static void coap_thread_process(void *a1, void *a2, void *a3)
{
    (void)a1;
    (void)a2;
    (void)a3;

    int sock = start_coap_server();
    if (sock < 0) {
        return;
    }

    while (1) {
        process_client_request(sock);
    }
}

static void coaps_thread_process(void *a1, void *a2, void *a3)
{
    (void)a1;
    (void)a2;
    (void)a3;

    int sock = start_coaps_server();
    if (sock < 0) {
        return;
    }

    while (1) {
        process_client_request(sock);
    }
}


void coap_server_init(coap_rsrcs_getter_t rsrcs_getter)
{
    rsrcs_get = rsrcs_getter;

    k_thread_start(coap_thread_id);
    k_thread_start(coaps_thread_id);
}
