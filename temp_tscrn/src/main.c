/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdbool.h>

#include "coap.h"
#include "conn.h"
#include "ctlr.h"
#include "data_dispatcher.h"
#include "dfu_utils.h"
#include "display.h"
#include "light_conn.h"
#include "output.h"
#include "prj_timeout.h"
#include "prov.h"
#include "rmt_out.h"
#include "sensor.h"
#include "shades_conn.h"
#include "vent_conn.h"

#include <net/fota_download.h>
#include <openthread/thread.h>
#include <zephyr/drivers/misc/ft8xx/ft8xx.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/net/openthread.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>

#define TX_POWER 8

static struct ft8xx_touch_transform tt;
static bool tt_known = false;

static int app_settings_set(const char *name, size_t len,
                            settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    int rc;

    if (settings_name_steq(name, "tt", &next) && !next) {
        if (len != sizeof(tt)) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, &tt, sizeof(tt));
        if (rc >= 0) {
            tt_known = true;
            return 0;
        }

        return rc;
    }

    return -ENOENT;
}

struct settings_handler sett_app_conf = {
    .name = "app",
    .h_set = app_settings_set,
};

void fota_callback(const struct fota_download_evt *evt)
{
    if (evt->id == FOTA_DOWNLOAD_EVT_FINISHED) {
        sys_reboot(SYS_REBOOT_COLD);
    }
}

int main(void)
{
    int r;
    prov_init();

    r = settings_subsys_init();
    r = settings_register(&sett_app_conf);
    r = settings_register(prov_get_settings_handler());
    r = settings_load();

    otError error;
    struct otInstance *ot_instance = openthread_get_default_instance();

    error = otPlatRadioSetTransmitPower(ot_instance, TX_POWER);
    assert(error == OT_ERROR_NONE);

    struct otIp6Address site_local_all_nodes_addr = {.mFields = {.m8 =
            {0xff, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01} }};

    error = otIp6SubscribeMulticastAddress(ot_instance, &site_local_all_nodes_addr);
    assert(error == OT_ERROR_NONE);

    // TODO: Why is this delay needed?
    k_sleep(K_MSEC(50));

#if CONFIG_BOARD_TEMP_TSCRN
    if (tt_known) {
        ft8xx_touch_transform_set(&tt);
    } else {
        ft8xx_calibrate(&tt);
        settings_save_one("app/tt", &tt, sizeof(tt));
    }
#endif

    fota_download_init(fota_callback);
    data_dispatcher_init();
    conn_init();
    display_init();
    sensor_init();
    output_init();
    ctlr_init();
    coap_init();
    rmt_out_init();
    vent_conn_init();
    light_conn_init();
    shades_conn_init();
    prj_timeout_init();


#if 0
    if (dfu_utils_keep_checking_conectivity_until(k_uptime_get() + 1000UL * 60UL * 5UL))
#else
    k_sleep(K_MSEC(1000UL * 60UL * 2UL)); // Wait two minutes to allow forcing rollback by power cycle
#endif
    {
        boot_write_img_confirmed();
    }

    return 0;
}
