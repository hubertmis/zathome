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

void continuous_sd_debug(int *state, int64_t *target_time,
		const char **name, const char **type, int *sd_missed,
		int64_t *last_req_ts, int64_t *last_rsp_ts,
		int *last_sem_take_result,
		k_ticks_t *remaining_thread_ticks);

#ifdef __cplusplus
}   
#endif

#endif // CONTINUOUS_SD_H_

