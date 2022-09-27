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
#include "notification.h"

#define SETT_NAME "prov"
#define RSRC_NAME "r"
#define OUT_NAME "o"
#define RSRC_TYPE "prj"

#define OUT_NAME_MAX_LEN (sizeof(OUT_NAME) + 2)

static char rsrc_label[PROV_LBL_MAX_LEN];
static const char rsrc_type[] = RSRC_TYPE;

static char out_labels[PROV_NUM_OUTS][PROV_LBL_MAX_LEN];

static int get_out_name(char *out_name, size_t size, int id)
{
	return snprintf(out_name, size, "%s%d", OUT_NAME, id);
}

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

	strncpy(out_labels[id], new_out_label, PROV_LBL_MAX_LEN);
	return 0;
}

const char *prov_get_out_label(int id)
{
	if (id < 0 || id >= PROV_NUM_OUTS) {
		return NULL;
	}

	return out_labels[id];
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

	rc = read_cb(cb_arg, out_labels[id], PROV_LBL_MAX_LEN);

	if (rc < 0) {
		return rc;
	}

	out_labels[id][rc] = '\0';
	notification_add_target(out_labels[id]);

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

    for (int i = 0; i < PROV_NUM_OUTS; i++) {
        char out_label[OUT_NAME_MAX_LEN];
        rc = get_out_name(out_label, sizeof(out_label), i);
	if ((rc < 0) || (rc >= sizeof(out_label))) {
		continue;
	}

        if (settings_name_steq(name, out_label, &next) && !next) {
            return set_out_from_nvm(i, len, read_cb, cb_arg);
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
        for (int i = 0; i < PROV_NUM_OUTS; i++) {
            int r;
            char out_label[OUT_NAME_MAX_LEN];
	    char sett_label[sizeof(SETT_NAME) + 1 + OUT_NAME_MAX_LEN];
            r = get_out_name(out_label, sizeof(out_label), i);
	    if ((r < 0) || (r >= sizeof(out_label))) {
		    continue;
	    }
	    r = snprintf(sett_label, sizeof(sett_label), "%s/%s", SETT_NAME, out_label);
	    if ((r < 0) || (r >= sizeof(sett_label))) {
		    continue;
	    }
            settings_save_one(sett_label, out_labels[i], strlen(out_labels[i]));

		if (strlen(out_labels[i])) {
			notification_add_target(out_labels[i]);
		}
        }
	coap_sd_server_clear_all_rsrcs();
	coap_sd_server_register_rsrc(rsrc_label, rsrc_type);

	notification_reset_targets();
	for (int i = 0; i < PROV_NUM_OUTS; i++) {
	}
}

struct settings_handler *prov_get_settings_handler(void)
{
    return &sett_conf;
}
