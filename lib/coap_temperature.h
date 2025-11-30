/*
 * Copyright (c) 2025 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief CoAP temperature measurement service
 */

#ifndef COAP_TEMPERATURE_H_
#define COAP_TEMPERATURE_H_

#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

int coap_temperature_get(struct coap_resource *resource,
		struct coap_packet *request,
		struct sockaddr *addr, socklen_t addr_len);

#ifdef __cplusplus
}
#endif

#endif // COAP_TEMPERATURE_H_
