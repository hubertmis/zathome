/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Provisioning settings
 */
    
#ifndef PROV_H_
#define PROV_H_

#ifdef __cplusplus
extern "C" {
#endif

#define PROV_LBL_MAX_LEN 6
#define PROV_RSRC_NUM 2

void prov_init(void);
int prov_set_rsrc_label(int id, const char *rsrc_label);
const char *prov_get_rsrc_label(int id);
int prov_set_rsrc_duration(int id, int duration);
int prov_get_rsrc_duration(int id);
int prov_set_swing_interval(int id, int interval);
int prov_get_swing_interval(int id);
void prov_store(void);
struct settings_handler *prov_get_settings_handler(void);

#ifdef __cplusplus
}   
#endif

#endif // PROV_H_


