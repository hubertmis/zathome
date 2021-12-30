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
#include "data_dispatcher.h"

#define SETT_NAME "prov"
#define RSRC0_NAME "r0"
#define RSRC1_NAME "r1"
#define RSRC_TYPE "tempcnt"
#define OUT0_NAME "o0"

static const char rsrc_type[] = RSRC_TYPE;
static char rsrc_labels[DATA_LOC_NUM][PROV_LBL_MAX_LEN];
static char loc_output_label[PROV_LBL_MAX_LEN];

void prov_init(void)
{
    for (int i = 0; i < DATA_LOC_NUM; ++i) {
        rsrc_labels[i][0] = '\0';
    }

    loc_output_label[0] = '\0';
}

int prov_set_rsrc_label(data_loc_t loc, const char *rsrc_label)
{
    if (loc >= DATA_LOC_NUM) {
        return -1;
    }

    if (strlen(rsrc_label) >= PROV_LBL_MAX_LEN) {
        return -2;
    }

    strncpy(rsrc_labels[loc], rsrc_label, PROV_LBL_MAX_LEN);
    return 0;
}

const char *prov_get_rsrc_label(data_loc_t loc)
{
    if (loc < DATA_LOC_NUM) {
        return rsrc_labels[loc];
    }

    return NULL;
}

int prov_set_loc_output_label(const char *label)
{
    if (strlen(label) >= PROV_LBL_MAX_LEN) {
        return -2;
    }

    strncpy(loc_output_label, label, PROV_LBL_MAX_LEN);
    return 0;
}

const char *prov_get_loc_output_label(void)
{
    return loc_output_label;
}

static int prov_set_from_nvm(const char *name, size_t len,
                             settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    int rc;

    if (settings_name_steq(name, RSRC0_NAME, &next) && !next) {
        if (len >= PROV_LBL_MAX_LEN) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, rsrc_labels[0], PROV_LBL_MAX_LEN);

        if (rc < 0) {
            return rc;
        }

        rsrc_labels[0][rc] = '\0';
	if (strlen(rsrc_labels[0])) coap_sd_server_register_rsrc(rsrc_labels[0], rsrc_type);

        return 0;
    }

    if (settings_name_steq(name, RSRC1_NAME, &next) && !next) {
        if (len >= PROV_LBL_MAX_LEN) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, rsrc_labels[1], PROV_LBL_MAX_LEN);

        if (rc < 0) {
            return rc;
        }

        rsrc_labels[1][rc] = '\0';
	if (strlen(rsrc_labels[1])) coap_sd_server_register_rsrc(rsrc_labels[1], rsrc_type);

        return 0;
    }

    if (settings_name_steq(name, OUT0_NAME, &next) && !next) {
        if (len >= PROV_LBL_MAX_LEN) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, loc_output_label, PROV_LBL_MAX_LEN);
        loc_output_label[rc] = '\0';

        if (rc < 0) {
            return rc;
        }

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
    settings_save_one(SETT_NAME "/" RSRC0_NAME, rsrc_labels[0], strlen(rsrc_labels[0]));
    settings_save_one(SETT_NAME "/" RSRC1_NAME, rsrc_labels[1], strlen(rsrc_labels[1]));
    settings_save_one(SETT_NAME "/" OUT0_NAME, loc_output_label, strlen(loc_output_label));

    coap_sd_server_clear_all_rsrcs();
    if (strlen(rsrc_labels[0])) coap_sd_server_register_rsrc(rsrc_labels[0], rsrc_type);
    if (strlen(rsrc_labels[1])) coap_sd_server_register_rsrc(rsrc_labels[1], rsrc_type);
}

struct settings_handler *prov_get_settings_handler(void)
{
    return &sett_conf;
}
