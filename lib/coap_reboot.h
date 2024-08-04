/*
 * Copyright (c) 2023 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief CoAP reboot service
 */
    
#ifndef COAP_REBOOT_H_
#define COAP_REBOOT_H_

#include <net/coap.h>
#include <net/fota_download.h>
#include <net/socket.h>
    
#ifdef __cplusplus
extern "C" {
#endif

int coap_reboot_post(struct coap_resource *resource,
		struct coap_packet *request,
		struct sockaddr *addr, socklen_t addr_len);

#ifdef __cplusplus
}   
#endif

#endif // COAP_REBOOT_H_
