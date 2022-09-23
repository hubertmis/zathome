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
#include <net/fota_download.h>
#include <net/socket.h>
    
#ifdef __cplusplus
extern "C" {
#endif

enum {
	COAP_FOTA_EVT_STARTED,
	COAP_FOTA_EVT_FINISHED,
};

struct coap_fota_evt {
	int evt;
};

typedef void (*coap_fota_cb_t)(const struct coap_fota_evt *evt);

void coap_fota_callback(const struct fota_download_evt *evt);

int coap_fota_get(struct coap_resource *resource,
		struct coap_packet *request,
		struct sockaddr *addr, socklen_t addr_len);

int coap_fota_post(struct coap_resource *resource,
		struct coap_packet *request,
		struct sockaddr *addr, socklen_t addr_len);

int coap_fota_register_cb(coap_fota_cb_t cb);

#ifdef __cplusplus
}   
#endif

#endif // COAP_FOTA_H_

