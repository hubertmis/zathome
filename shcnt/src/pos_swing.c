/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pos_swing.h"

#include <stdint.h>
#include <sys/util.h>
#include "mot_cnt.h"
#include "pos_srv.h"
#include "prov.h"

#define NUM_DEV DT_NUM_INST_STATUS_OKAY(hubertmis_mot_cnt)

#define POS_SWING_STACK_SIZE 512
#define POS_SWING_THR_PRIO 4

void pos_swing_thread(void *, void *, void *);

K_THREAD_DEFINE(pos_swing_tid_0, POS_SWING_STACK_SIZE,
		pos_swing_thread, (void *)0, NULL, NULL,
		POS_SWING_THR_PRIO, 0, 0);
K_THREAD_DEFINE(pos_swing_tid_1, POS_SWING_STACK_SIZE,
		pos_swing_thread, (void *)1, NULL, NULL,
		POS_SWING_THR_PRIO, 0, 0);

static uint32_t interval[NUM_DEV];
static struct k_sem sem[NUM_DEV];

void pos_swing_thread(void * id_val, void *, void *)
{
	int id = (int)id_val;

	if (id < 0 || id >= NUM_DEV) {
		return;
	}

	interval[id] = 0;
	k_sem_init(&sem[id], 0, 1);

	while (1) {
		int duration = prov_get_rsrc_duration(id);
		int interval = prov_get_swing_interval(id);

		if (duration < 0 || interval < 0) {
			k_sem_take(&sem[id], K_FOREVER);
			continue;
		}

		if (interval) {
			duration = duration == 0 ? MOT_CNT_DEFAULT_TIME : duration * 3 / 2;
			duration += 2000;

			pos_srv_override(id, MOT_CNT_MAX);
			k_sleep(K_MSEC(duration));
			pos_srv_override(id, MOT_CNT_MIN);
			k_sleep(K_MSEC(duration));
			pos_srv_override_release(id);

			k_sem_take(&sem[id], K_MSEC(interval));
		} else {
			k_sem_take(&sem[id], K_FOREVER);
		}
	}
}

int pos_swing_interval_set(int id, int s)
{
	if (id < 0 || id >= NUM_DEV) {
		return -EINVAL;
	}

	interval[id] = s;
	k_sem_give(&sem[id]);

	return 0;
}
