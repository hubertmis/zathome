/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief CoAP Server
 */
    
#ifndef COAP_SERVER_H_
#define COAP_SERVER_H_

#include <net/coap.h>
#include <tinycbor/cbor.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct coap_resource * (*coap_rsrcs_getter_t)(int sock);
typedef int (*coap_server_cbor_map_handler_t)(CborValue *value,
	       	enum coap_response_code *rsp_code, void *context);

void coap_server_init(coap_rsrcs_getter_t rsrcs_getter);

int coap_server_send_coap_reply(int sock,
               struct coap_packet *cpkt,
               const struct sockaddr *addr,
               socklen_t addr_len);

int coap_server_send_ack(int sock, const struct sockaddr *addr, socklen_t addr_len,
                    uint16_t id, enum coap_response_code code, uint8_t *token, uint8_t tkl);
int coap_server_send_ack_with_payload(int sock, const struct sockaddr *addr, socklen_t addr_len,
                    uint16_t id, enum coap_response_code code, uint8_t *token, uint8_t tkl,
                    const uint8_t *payload, size_t payload_len);
int coap_server_handle_simple_getter(int sock, const struct coap_resource *resource,
                    const struct coap_packet *request,
                    const struct sockaddr *addr, socklen_t addr_len,
                    const uint8_t *payload, size_t payload_len);
int coap_server_handle_simple_setter(int sock, const struct coap_resource *resource,
                    const struct coap_packet *request,
                    const struct sockaddr *addr, socklen_t addr_len,
		    coap_server_cbor_map_handler_t cbor_map_handler, void *context);

#ifdef __cplusplus
}   
#endif

#endif // COAP_SERVER_H_
