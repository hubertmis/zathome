/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Provisioning settings
 */
    
#ifndef PROV_H_
#define PROV_H_

#include <settings/settings.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROV_LBL_MAX_LEN 6

void prov_init(void);
int prov_set_rsrc_label(const char *rsrc_label);
const char *prov_get_rsrc_label(void);
void prov_store(void);
struct settings_handler *prov_get_settings_handler(void);

#ifdef __cplusplus
}   
#endif

#endif // PROV_H_



