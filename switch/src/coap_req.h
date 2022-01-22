/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief CoAP RGBW requestor
 */
    
#ifndef COAP_REQ_H_
#define COAP_REQ_H_

#include <net/socket.h>
    
#ifdef __cplusplus
extern "C" {
#endif

int coap_req_preset(struct in6_addr *addr, const char *rsrc, int preset_id);

#ifdef __cplusplus
}   
#endif

#endif // COAP_REQ_H_

