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
#define PROV_NUM_PRESETS 8

struct prov_leds_brightness {
	unsigned r;
	unsigned g;
	unsigned b;
	unsigned w;
};

void prov_init(void);
int prov_set_rsrc_label(const char *rsrc_label);
const char *prov_get_rsrc_label(void);
int prov_set_preset(int preset_id, const struct prov_leds_brightness *leds);
int prov_get_preset(int preset_id, struct prov_leds_brightness *leds);
void prov_store(void);
struct settings_handler *prov_get_settings_handler(void);

#ifdef __cplusplus
}   
#endif

#endif // PROV_H_


