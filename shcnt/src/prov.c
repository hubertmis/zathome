/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "prov.h"

#include <coap_sd.h>
#include "mot_cnt.h"
#include "mot_cnt_map.h"
#include "pos_swing.h"

#include <errno.h>
#include <string.h>
#include <settings/settings.h>

#define SETT_NAME "prov"
#define RSRC0_NAME "r0"
#define RSRC1_NAME "r1"
#define RSRC_TYPE "shcnt"
#define DUR0_NAME "d0"
#define DUR1_NAME "d1"
#define SW_INT0_NAME "i0"
#define SW_INT1_NAME "i1"

static char rsrc_label[PROV_RSRC_NUM][PROV_LBL_MAX_LEN];
static const char rsrc_type[] = RSRC_TYPE;
static int rsrc_duration[PROV_RSRC_NUM];
static int swing_interval[PROV_RSRC_NUM];

void prov_init(void)
{
	for (int i = 0; i < PROV_RSRC_NUM; i++) {
		rsrc_label[i][0] = '\0';
		rsrc_duration[i] = 0;
		swing_interval[i] = 0;
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

int prov_set_rsrc_duration(int id, int duration)
{
	if (id < 0 || id >= PROV_RSRC_NUM) {
		return -1;
	}

	rsrc_duration[id] = duration;
	return 0;
}

int prov_get_rsrc_duration(int id)
{
	if (id < 0 || id >= PROV_RSRC_NUM) {
		return -1;
	}

	return rsrc_duration[id];
}

int prov_set_swing_interval(int id, int interval)
{
	if (id < 0 || id >= PROV_RSRC_NUM) {
		return -1;
	}

	swing_interval[id] = interval;
	return 0;
}

int prov_get_swing_interval(int id)
{
	if (id < 0 || id >= PROV_RSRC_NUM) {
		return -1;
	}

	return swing_interval[id];
}

static int prov_read_label_from_nvm(size_t len, settings_read_cb read_cb, void *cb_arg, char *buffer)
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

	if (strlen(buffer)) coap_sd_server_register_rsrc(buffer, rsrc_type);

	return 0;
}

static int prov_read_duration_from_nvm(size_t len, settings_read_cb read_cb, void *cb_arg, int id)
{
	int rc;

	if (id < 0 || id >= PROV_RSRC_NUM) {
		return -EINVAL;
	}

	if (len != sizeof(rsrc_duration[0])) {
		return -EINVAL;
	}

	rc = read_cb(cb_arg, rsrc_duration + id, sizeof(rsrc_duration[0]));

	if (rc < 0) {
		return rc;
	}

	const struct device *mot_cnt = mot_cnt_map_from_id(id);
	const struct mot_cnt_api *api = mot_cnt->api;
	api->set_run_time(mot_cnt, rsrc_duration[id]);

	return 0;
}

static int prov_read_swing_interval_from_nvm(size_t len, settings_read_cb read_cb, void *cb_arg, int id)
{
	int rc;

	if (id < 0 || id >= PROV_RSRC_NUM) {
		return -EINVAL;
	}

	if (len != sizeof(swing_interval[0])) {
		return -EINVAL;
	}

	rc = read_cb(cb_arg, swing_interval + id, sizeof(swing_interval[0]));

	if (rc < 0) {
		return rc;
	}

	pos_swing_interval_set(id, swing_interval[id]);

	return 0;
}

static int prov_set_from_nvm(const char *name, size_t len,
                             settings_read_cb read_cb, void *cb_arg)
{
    const char *next;

    if (settings_name_steq(name, RSRC0_NAME, &next) && !next) {
		return prov_read_label_from_nvm(len, read_cb, cb_arg, rsrc_label[0]);
    }

    if (settings_name_steq(name, RSRC1_NAME, &next) && !next) {
		return prov_read_label_from_nvm(len, read_cb, cb_arg, rsrc_label[1]);
    }

    if (settings_name_steq(name, DUR0_NAME, &next) && !next) {
		return prov_read_duration_from_nvm(len, read_cb, cb_arg, 0);
    }

    if (settings_name_steq(name, DUR1_NAME, &next) && !next) {
		return prov_read_duration_from_nvm(len, read_cb, cb_arg, 1);
    }

    if (settings_name_steq(name, SW_INT0_NAME, &next) && !next) {
	    return prov_read_swing_interval_from_nvm(len, read_cb, cb_arg, 0);
    }

    if (settings_name_steq(name, SW_INT1_NAME, &next) && !next) {
	    return prov_read_swing_interval_from_nvm(len, read_cb, cb_arg, 1);
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
	settings_save_one(SETT_NAME "/" DUR0_NAME, rsrc_duration + 0, sizeof(rsrc_duration[0]));
	settings_save_one(SETT_NAME "/" DUR1_NAME, rsrc_duration + 1, sizeof(rsrc_duration[1]));
	settings_save_one(SETT_NAME "/" SW_INT0_NAME, swing_interval + 0, sizeof(swing_interval[0]));
	settings_save_one(SETT_NAME "/" SW_INT1_NAME, swing_interval + 1, sizeof(swing_interval[1]));

	coap_sd_server_clear_all_rsrcs();
	if (strlen(rsrc_label[0])) coap_sd_server_register_rsrc(rsrc_label[0], rsrc_type);
	if (strlen(rsrc_label[1])) coap_sd_server_register_rsrc(rsrc_label[1], rsrc_type);
	
	const struct device *mot_cnt = mot_cnt_map_from_id(0);
	const struct mot_cnt_api *api = mot_cnt->api;
	api->set_run_time(mot_cnt, rsrc_duration[0]);

	mot_cnt = mot_cnt_map_from_id(1);
	api->set_run_time(mot_cnt, rsrc_duration[1]);

	pos_swing_interval_set(0, swing_interval[0]);
	pos_swing_interval_set(1, swing_interval[1]);
}

struct settings_handler *prov_get_settings_handler(void)
{
    return &sett_conf;
}
