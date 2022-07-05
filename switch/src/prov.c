/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "prov.h"

#include <errno.h>
#include <string.h>
#include <settings/settings.h>

#include "analog_switch.h"
#include <coap_sd.h>
#include <continuous_sd.h>

#define SETT_NAME "prov"
#define RSRC0_NAME "r0"
#define RSRC1_NAME "r1"
#define RSRC_TYPE "sw"
#define OUT0_NAME "o0"
#define OUT1_NAME "o1"
#define OUT_TYPE "rgbw"
#define ANALOG0_NAME "a0"
#define ANALOG1_NAME "a1"

static const char rsrc_type[] = RSRC_TYPE;
static const char output_type[] = OUT_TYPE;
static char rsrc_labels[PROV_RSRC_NUM][PROV_LBL_MAX_LEN];
static char output_labels[PROV_RSRC_NUM][PROV_LBL_MAX_LEN];
static bool analog_enable[PROV_RSRC_NUM];

void prov_init(void)
{
    for (int i = 0; i < PROV_RSRC_NUM; ++i) {
        rsrc_labels[i][0] = '\0';
        output_labels[i][0] = '\0';
    }
}

int prov_set_rsrc_label(int rsrc_id, const char *rsrc_label)
{
    if (rsrc_id >= PROV_RSRC_NUM) {
        return -1;
    }

    if (strlen(rsrc_label) >= PROV_LBL_MAX_LEN) {
        return -2;
    }

    strncpy(rsrc_labels[rsrc_id], rsrc_label, PROV_LBL_MAX_LEN);
    return 0;
}

const char *prov_get_rsrc_label(int rsrc_id)
{
    if (rsrc_id < PROV_RSRC_NUM) {
        return rsrc_labels[rsrc_id];
    }

    return NULL;
}

int prov_set_output_rsrc_label(int rsrc_id, const char *label)
{
    if (rsrc_id >= PROV_RSRC_NUM) {
        return -1;
    }

    if (strlen(label) >= PROV_LBL_MAX_LEN) {
        return -2;
    }

    strncpy(output_labels[rsrc_id], label, PROV_LBL_MAX_LEN);
    return 0;
}

const char *prov_get_output_rsrc_label(int rsrc_id)
{
    if (rsrc_id >= PROV_RSRC_NUM) {
        return NULL;
    }

    return output_labels[rsrc_id];
}

int prov_set_analog_enabled(int rsrc_id, bool enabled)
{
	if (rsrc_id >= PROV_RSRC_NUM) {
		return -1;
	}
	
	analog_enable[rsrc_id] = enabled;
	return 0;
}

bool prov_get_analog_enabled(int rsrc_id)
{
	if (rsrc_id >= PROV_RSRC_NUM) {
		return -1;
	}

	return analog_enable[rsrc_id];
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

static int prov_read_output_from_nvm(size_t len, settings_read_cb read_cb, void *cb_arg, char *buffer)
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

	if (strlen(buffer)) continuous_sd_register(buffer, output_type, true);

	return 0;
}

static int prov_read_analog_enable_from_nvm(size_t len, settings_read_cb read_cb, void *cb_arg,
		bool *buffer, const struct device *dev)
{
	int rc;

	if (len != sizeof(*buffer)) {
		return -EINVAL;
	}

	rc = read_cb(cb_arg, buffer, sizeof(*buffer));

	if (rc < 0) {
		return rc;
	}

	if (*buffer) {
		const struct analog_switch_driver_api *api = dev->api;
		api->enable(dev);
	}

	return 0;
}

static int prov_set_from_nvm(const char *name, size_t len,
                             settings_read_cb read_cb, void *cb_arg)
{
    const char *next;

    if (settings_name_steq(name, RSRC0_NAME, &next) && !next) {
		return prov_read_label_from_nvm(len, read_cb, cb_arg, rsrc_labels[0]);
    }

    if (settings_name_steq(name, RSRC1_NAME, &next) && !next) {
		return prov_read_label_from_nvm(len, read_cb, cb_arg, rsrc_labels[1]);
    }

    if (settings_name_steq(name, OUT0_NAME, &next) && !next) {
		return prov_read_output_from_nvm(len, read_cb, cb_arg, output_labels[0]);
    }

    if (settings_name_steq(name, OUT1_NAME, &next) && !next) {
		return prov_read_output_from_nvm(len, read_cb, cb_arg, output_labels[1]);
    }

    if (settings_name_steq(name, ANALOG0_NAME, &next) && !next) {
		return prov_read_analog_enable_from_nvm(len, read_cb, cb_arg, &analog_enable[0], DEVICE_DT_GET(DT_NODELABEL(as1)));
    }

    if (settings_name_steq(name, ANALOG1_NAME, &next) && !next) {
		return prov_read_analog_enable_from_nvm(len, read_cb, cb_arg, &analog_enable[1], DEVICE_DT_GET(DT_NODELABEL(as2)));
    }

    return -ENOENT;
}

static struct settings_handler sett_conf = {
    .name = SETT_NAME,
    .h_set = prov_set_from_nvm,
};

void prov_store(void)
{
    const struct device *dev0 = DEVICE_DT_GET(DT_NODELABEL(as1));
    const struct device *dev1 = DEVICE_DT_GET(DT_NODELABEL(as2));
    const struct analog_switch_driver_api *api = dev0->api;

    settings_save_one(SETT_NAME "/" RSRC0_NAME, rsrc_labels[0], strlen(rsrc_labels[0]));
    settings_save_one(SETT_NAME "/" RSRC1_NAME, rsrc_labels[1], strlen(rsrc_labels[1]));
    settings_save_one(SETT_NAME "/" OUT0_NAME, output_labels[0], strlen(output_labels[0]));
    settings_save_one(SETT_NAME "/" OUT1_NAME, output_labels[1], strlen(output_labels[1]));
    settings_save_one(SETT_NAME "/" ANALOG0_NAME, &analog_enable[0], sizeof(analog_enable[0]));
    settings_save_one(SETT_NAME "/" ANALOG1_NAME, &analog_enable[1], sizeof(analog_enable[1]));

    coap_sd_server_clear_all_rsrcs();
    if (strlen(rsrc_labels[0])) coap_sd_server_register_rsrc(rsrc_labels[0], rsrc_type);
    if (strlen(rsrc_labels[1])) coap_sd_server_register_rsrc(rsrc_labels[1], rsrc_type);

    continuous_sd_unregister_all();
    if (strlen(output_labels[0])) continuous_sd_register(output_labels[0], output_type, true);
    if (strlen(output_labels[1])) continuous_sd_register(output_labels[1], output_type, true);

    // Disabling is possible only by reset right now
    if (analog_enable[0]) api->enable(dev0);
    if (analog_enable[1]) api->enable(dev1);
}

struct settings_handler *prov_get_settings_handler(void)
{
    return &sett_conf;
}

