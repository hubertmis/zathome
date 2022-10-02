
/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "prj_timeout.h"

#include <zephyr.h>
#include "data_dispatcher.h"

static struct k_timer projector_timers[DATA_LOC_NUM];
static struct k_work projector_invalidators[DATA_LOC_NUM];

static void timer_handler_prj(struct k_timer *timer_id)
{
	for (int i = 0; i < DATA_LOC_NUM; i++) {
		if (timer_id == &projector_timers[i]) {
			k_work_submit(&projector_invalidators[i]);
			break;
		}
	}
}

static void work_handler_prj(struct k_work *work)
{
	for (int i = 0; i < DATA_LOC_NUM; i++) {
		if (work == &projector_invalidators[i]) {
			data_dispatcher_publish_t out_data = {
				.type         = DATA_PRJ_ENABLED,
				.loc          = i,
				.prj_validity = 0,
			};

			data_dispatcher_publish(&out_data);
			break;
		}
	}
}

static void changed_prj(const data_dispatcher_publish_t *data)
{
    uint16_t validity_ms = data->prj_validity;
    data_loc_t loc = data->loc;

    if (loc >= DATA_LOC_NUM) {
        return;
    }

    if (data->prj_validity > 0) {
        k_timer_start(&projector_timers[loc], K_MSEC(validity_ms), K_NO_WAIT);
    }
}

static data_dispatcher_subscribe_t sbscr_prj_setting = {
    .callback = changed_prj,
};

void prj_timeout_init(void)
{
    for (int i = 0; i < DATA_LOC_NUM; i++) {
        k_timer_init(&projector_timers[i], timer_handler_prj, NULL);
        k_work_init(&projector_invalidators[i], work_handler_prj);
    }

    data_dispatcher_subscribe(DATA_PRJ_ENABLED, &sbscr_prj_setting);
}
