/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief CoAP FOTA protocol
 */
    
#ifndef COAP_FOTA_H_
#define COAP_FOTA_H_

#include <net/coap.h>
#include <net/socket.h>
    
#ifdef __cplusplus
extern "C" {
#endif

int coap_fota_get(struct coap_resource *resource,
		struct coap_packet *request,
		struct sockaddr *addr, socklen_t addr_len);

int coap_fota_post(struct coap_resource *resource,
		struct coap_packet *request,
		struct sockaddr *addr, socklen_t addr_len);

#ifdef __cplusplus
}   
#endif

#endif // COAP_FOTA_H_

