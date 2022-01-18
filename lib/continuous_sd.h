/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Module performing Service Discovery continuously
 */
    
#ifndef CONTINUOUS_SD_H_
#define CONTINUOUS_SD_H_

#include <net/socket.h>
    
#ifdef __cplusplus
extern "C" {
#endif

void continuous_sd_init(void);

/** @brief Register Service Discovery procedure to start performing
 *
 * The procedure is being performed in a loop. The Service Discovery result is stored in a local
 * cache and can be retrieved with @ref continuous_sd_get_addr.
 */
int continuous_sd_register(const char *name, const char *type, bool mesh);

/** @brief Stop performing Service Discovry procedure for given service
 */
int continuous_sd_unregister(const char *name, const char *type);

/** @brief Stop performing Service Discovry procedure for all services
 */
int continuous_sd_unregister_all(void);

/** @brief Get address of given service
 */
int continuous_sd_get_addr(const char *name, const char *type, struct in6_addr *addr);

#ifdef __cplusplus
}   
#endif

#endif // CONTINUOUS_SD_H_

