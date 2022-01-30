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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct coap_resource * (*coap_rsrcs_getter_t)(int sock);

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

#ifdef __cplusplus
}   
#endif

#endif // COAP_SERVER_H_
