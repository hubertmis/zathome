/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief CoAP Server
 */
    
#ifndef COAP_H_
#define COAP_H_

#include <net/socket.h>
    
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*coap_sd_found)(const struct sockaddr *src_addr,
                              const socklen_t *addrlen,
                              const char *name,
                              const char *type);


void coap_init(void);

/** @brief Run Service Discovery procedure
 *
 * Transmit multicast Service Discovery request and collect responses.
 * Each response causes call to @p cb callback. Caller must be ready to handle
 * callback any number of times (including 0) per single request.
 */
int coap_sd_start(const char *name, const char *type, coap_sd_found cb);

#ifdef __cplusplus
}   
#endif

#endif // COAP_H_

