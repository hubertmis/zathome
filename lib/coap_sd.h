/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief CoAP Service Discovery protocol
 */
    
#ifndef COAP_SD_H_
#define COAP_SD_H_

#include <net/coap.h>
#include <net/socket.h>
    
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*coap_sd_found)(const struct sockaddr *src_addr,
                              const socklen_t *addrlen,
                              const char *name,
                              const char *type);

/** @brief Run Service Discovery procedure
 *
 * Transmit multicast Service Discovery request and collect responses.
 * Each response causes call to @p cb callback. Caller must be ready to handle
 * callback any number of times (including 0) per single request.
 */
int coap_sd_start(const char *name, const char *type, coap_sd_found cb, bool mesh);

/** @brief Process CoAP SD Server request
 */
int coap_sd_server(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len);

int coap_sd_server_register_rsrc(const char *name, const char *type);
void coap_sd_server_clear_all_rsrcs(void);

#ifdef __cplusplus
}   
#endif

#endif // COAP_SD_H_
