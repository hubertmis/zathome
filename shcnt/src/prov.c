/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "prov.h"

#include <coap_sd.h>

#include <errno.h>
#include <string.h>
#include <settings/settings.h>

#define SETT_NAME "prov"
#define RSRC0_NAME "r0"
#define RSRC1_NAME "r1"
#define RSRC_TYPE "shcnt"

static char rsrc_label[PROV_RSRC_NUM][PROV_LBL_MAX_LEN];
static const char rsrc_type[] = RSRC_TYPE;

void prov_init(void)
{
	for (int i = 0; i < PROV_RSRC_NUM; i++) {
		rsrc_label[i][0] = '\0';
	}
}

int prov_set_rsrc_label(int id, const char *new_rsrc_label)
{
	if (id < 0 || id >= PROV_RSRC_NUM) {
		return -1;
	}
	if (strlen(new_rsrc_label) >= PROV_LBL_MAX_LEN) {
		return -2;
	}

	strncpy(rsrc_label[id], new_rsrc_label, PROV_LBL_MAX_LEN);
	return 0;
}

const char *prov_get_rsrc_label(int id)
{
	if (id < 0 || id >= PROV_RSRC_NUM) {
		return NULL;
	}
	return rsrc_label[id];
}

static int prov_read_from_nvm(size_t len, settings_read_cb read_cb, void *cb_arg, char *buffer)
{
	int rc;

	if (len >= PROV_LBL_MAX_LEN) {
		return -EINVAL;
	}

	rc = read_cb(cb_arg, buffer, PROV_LBL_MAX_LEN);

	if (rc < 0) {
		return rc;
	}

	buffer[rc] = '\0';

	return 0;
}

static int prov_set_from_nvm(const char *name, size_t len,
                             settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    int ret;

    if (settings_name_steq(name, RSRC0_NAME, &next) && !next) {
		ret = prov_read_from_nvm(len, read_cb, cb_arg, rsrc_label[0]);

		if (!ret) {
			coap_sd_server_register_rsrc(rsrc_label[0], rsrc_type);
		}

		return ret;
    }

    if (settings_name_steq(name, RSRC1_NAME, &next) && !next) {
		ret = prov_read_from_nvm(len, read_cb, cb_arg, rsrc_label[1]);

		if (!ret) {
			coap_sd_server_register_rsrc(rsrc_label[1], rsrc_type);
		}

		return ret;
    }

    return -ENOENT;
}

static struct settings_handler sett_conf = {
    .name = SETT_NAME,
    .h_set = prov_set_from_nvm,
};

void prov_store(void)
{
	settings_save_one(SETT_NAME "/" RSRC0_NAME, rsrc_label[0], strlen(rsrc_label[0]));
	settings_save_one(SETT_NAME "/" RSRC1_NAME, rsrc_label[1], strlen(rsrc_label[1]));
	coap_sd_server_clear_all_rsrcs();
	coap_sd_server_register_rsrc(rsrc_label[0], rsrc_type);
	coap_sd_server_register_rsrc(rsrc_label[1], rsrc_type);
}

struct settings_handler *prov_get_settings_handler(void)
{
    return &sett_conf;
}
