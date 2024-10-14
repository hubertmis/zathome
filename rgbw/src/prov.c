/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "prov.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <settings/settings.h>

#include <coap_sd.h>

#define SETT_NAME "prov"
#define RSRC_NAME "r"
#define RSRC_TYPE "rgbw"
#define PRESET_NAME "p"
#define MAX_PRESET_NAME_SIZE (sizeof(PRESET_NAME) + 2)

static char rsrc_label[PROV_LBL_MAX_LEN];
static const char rsrc_type[] = RSRC_TYPE;
static struct prov_leds_brightness presets[PROV_NUM_PRESETS];

void prov_init(void)
{
    rsrc_label[0] = '\0';

    for (int i = 0; i < PROV_NUM_PRESETS; i++) {
        presets[i].r = 0;
        presets[i].g = 0;
        presets[i].b = 0;
        presets[i].w = 0;
    }
}

int prov_set_rsrc_label(const char *new_rsrc_label)
{
    if (strlen(new_rsrc_label) >= PROV_LBL_MAX_LEN) {
        return -2;
    }

    strncpy(rsrc_label, new_rsrc_label, PROV_LBL_MAX_LEN);
    return 0;
}

const char *prov_get_rsrc_label(void)
{
    return rsrc_label;
}

int prov_set_preset(int preset_id, const struct prov_leds_brightness *leds)
{
    if (preset_id < 0 || preset_id >= PROV_NUM_PRESETS) {
        return -1;
    }

    presets[preset_id] = *leds;
    return 0;
}

int prov_get_preset(int preset_id, struct prov_leds_brightness *leds)
{
    if (preset_id < 0 || preset_id >= PROV_NUM_PRESETS) {
        return -1;
    }

    struct prov_leds_brightness *preset = &presets[preset_id];

    if (preset->r == 0 && preset->g == 0 && preset->b == 0 && preset->w == 0) {
        return -ENOENT;
    }

    *leds = presets[preset_id];
    return 0;
}

static int prov_set_from_nvm(const char *name, size_t len,
                             settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    int rc;

    if (settings_name_steq(name, RSRC_NAME, &next) && !next) {
        if (len >= PROV_LBL_MAX_LEN) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, rsrc_label, PROV_LBL_MAX_LEN);

        if (rc < 0) {
            return rc;
        }

        rsrc_label[rc] = '\0';
        coap_sd_server_register_rsrc(rsrc_label, rsrc_type);

        return 0;
    }

    for (int i = 0; i < PROV_NUM_PRESETS; i++) {
        char preset_name[MAX_PRESET_NAME_SIZE];
        int preset_name_len = snprintf(preset_name, sizeof(preset_name), PRESET_NAME "%d", i);

        if (preset_name_len < 0 || preset_name_len >= sizeof(preset_name)) {
            continue;
        }

        if (settings_name_steq(name, preset_name, &next) && !next) {
            if (len != sizeof(presets[i])) {
                return -EINVAL;
            }

            rc = read_cb(cb_arg, &presets[i], sizeof(presets[i]));

            if (rc < 0) {
                return rc;
            }

            return 0;
        }
    }

    return -ENOENT;
}

static struct settings_handler sett_conf = {
    .name = SETT_NAME,
    .h_set = prov_set_from_nvm,
};

void prov_store(void)
{
    settings_save_one(SETT_NAME "/" RSRC_NAME, rsrc_label, strlen(rsrc_label));
    for (int i = 0; i < PROV_NUM_PRESETS; i++) {
        char preset_name[sizeof(SETT_NAME) + 1 + MAX_PRESET_NAME_SIZE];
        int preset_name_len = snprintf(preset_name, sizeof(preset_name), SETT_NAME "/" PRESET_NAME "%d", i);

        if (preset_name_len < 0 || preset_name_len >= sizeof(preset_name)) {
            continue;
        }

        settings_save_one(preset_name, &presets[i], sizeof(presets[i]));
    }
    coap_sd_server_clear_all_rsrcs();
    coap_sd_server_register_rsrc(rsrc_label, rsrc_type);
}

struct settings_handler *prov_get_settings_handler(void)
{
    return &sett_conf;
}
