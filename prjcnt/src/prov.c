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
#include "notification.h"

#define SETT_NAME "prov"
#define RSRC_NAME "r"
#define OUT0_NAME "o0"
#define OUT1_NAME "o1"
#define OUT2_NAME "o2"
#define OUT3_NAME "o3"
#define RSRC_TYPE "prj"

static char rsrc_label[PROV_LBL_MAX_LEN];
static const char rsrc_type[] = RSRC_TYPE;

static char out_label[PROV_NUM_OUTS][PROV_LBL_MAX_LEN];

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

int prov_set_out_label(int id, const char *new_out_label)
{
	if (id < 0 || id >= PROV_NUM_OUTS) {
		return -1;
	}
	if (strlen(new_out_label) >= PROV_LBL_MAX_LEN) {
		return -2;
	}

	strncpy(out_label[id], new_out_label, PROV_LBL_MAX_LEN);
	return 0;
}

const char *prov_get_out_label(int id)
{
	if (id < 0 || id >= PROV_NUM_OUTS) {
		return NULL;
	}

	return out_label[id];
}

static int set_out_from_nvm(int id, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	int rc;

	if (id < 0 || id >= PROV_NUM_OUTS) {
		return -EINVAL;
	}
	if (len >= PROV_LBL_MAX_LEN) {
		return -EINVAL;
	}

	rc = read_cb(cb_arg, out_label[id], PROV_LBL_MAX_LEN);

	if (rc < 0) {
		return rc;
	}

	out_label[id][rc] = '\0';
	notification_add_target(out_label[id]);

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

    if (settings_name_steq(name, OUT0_NAME, &next) && !next) {
	return set_out_from_nvm(0, len, read_cb, cb_arg);
    }

    if (settings_name_steq(name, OUT1_NAME, &next) && !next) {
	return set_out_from_nvm(1, len, read_cb, cb_arg);
    }

    if (settings_name_steq(name, OUT2_NAME, &next) && !next) {
	return set_out_from_nvm(2, len, read_cb, cb_arg);
    }

    if (settings_name_steq(name, OUT3_NAME, &next) && !next) {
	return set_out_from_nvm(3, len, read_cb, cb_arg);
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
	settings_save_one(SETT_NAME "/" OUT0_NAME, out_label[0], strlen(out_label[0]));
	settings_save_one(SETT_NAME "/" OUT1_NAME, out_label[1], strlen(out_label[1]));
	settings_save_one(SETT_NAME "/" OUT2_NAME, out_label[2], strlen(out_label[2]));
	settings_save_one(SETT_NAME "/" OUT3_NAME, out_label[3], strlen(out_label[3]));
	coap_sd_server_clear_all_rsrcs();
	coap_sd_server_register_rsrc(rsrc_label, rsrc_type);

	notification_reset_targets();
	for (int i = 0; i < PROV_NUM_OUTS; i++) {
		if (strlen(out_label[i])) {
			notification_add_target(out_label[i]);
		}
	}
}

struct settings_handler *prov_get_settings_handler(void)
{
    return &sett_conf;
}
