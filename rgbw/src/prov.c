/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "prov.h"

#include <errno.h>
#include <string.h>
#include <settings/settings.h>

#include <coap_sd.h>

#define SETT_NAME "prov"
#define RSRC_NAME "r"
#define RSRC_TYPE "rgbw"

static char rsrc_label[PROV_LBL_MAX_LEN];
static const char rsrc_type[] = RSRC_TYPE;

void prov_init(void)
{
    rsrc_label[0] = '\0';
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

    return -ENOENT;
}

static struct settings_handler sett_conf = {
    .name = SETT_NAME,
    .h_set = prov_set_from_nvm,
};

void prov_store(void)
{
    settings_save_one(SETT_NAME "/" RSRC_NAME, rsrc_label, strlen(rsrc_label));
	coap_sd_server_clear_all_rsrcs();
	coap_sd_server_register_rsrc(rsrc_label, rsrc_type);
}

struct settings_handler *prov_get_settings_handler(void)
{
    return &sett_conf;
}
