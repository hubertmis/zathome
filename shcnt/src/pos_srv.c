/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pos_srv.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/util.h>
#include "mot_cnt.h"
#include "mot_cnt_map.h"

#define NUM_DEV DT_NUM_INST_STATUS_OKAY(hubertmis_mot_cnt)

#define POS_SRV_STACK_SIZE 512
#define POS_SRV_THR_PRIO 4

static void pos_srv_thread(void *, void *, void *);

K_THREAD_DEFINE(pos_srv_tid_0, POS_SRV_STACK_SIZE,
		pos_srv_thread, (void *)0, NULL, NULL,
		POS_SRV_THR_PRIO, 0, 0);
K_THREAD_DEFINE(pos_srv_tid_1, POS_SRV_STACK_SIZE,
		pos_srv_thread, (void *)1, NULL, NULL,
		POS_SRV_THR_PRIO, 0, 0);

static int requests[NUM_DEV];
static int overridden[NUM_DEV];
static struct k_sem req_sem[NUM_DEV];

static int64_t request_timestamps[NUM_DEV];
static int64_t prj_timestamps[NUM_DEV];

static struct k_timer projector_timers[NUM_DEV];
static struct k_work projector_invalidators[NUM_DEV];

static int get_val(int id)
{
	int ovr_val = overridden[id];
	int req_val = requests[id];
	int64_t req_ts = request_timestamps[id];
	int64_t prj_ts = prj_timestamps[id];

	if (ovr_val != MOT_CNT_STOP) {
		return ovr_val;
	}

	if ((prj_ts > req_ts) /* TODO: and it is between dawn and dusk */) {
		return MOT_CNT_MAX;
	}

	return req_val;
}

static void pos_srv_thread(void * id_val, void *, void *)
{
	int id = (int)id_val;
	const struct device *mot_cnt = mot_cnt_map_from_id(id);

	if (id < 0 || id >= NUM_DEV || !mot_cnt) {
		return;
	}

	const struct mot_cnt_api *api = mot_cnt->api;
	requests[id] = MOT_CNT_STOP;
	overridden[id] = MOT_CNT_STOP;
	k_sem_init(&req_sem[id], 0, 1);

	while (1) {
		k_sem_take(&req_sem[id], K_FOREVER);
		api->go_to(mot_cnt, get_val(id));
	}
}

static void timer_handler_prj(struct k_timer *timer_id)
{
	for (int i = 0; i < NUM_DEV; i++) {
		if (timer_id == &projector_timers[i]) {
			k_work_submit(&projector_invalidators[i]);
			break;
		}
	}
}

static void work_handler_prj(struct k_work *work)
{
	for (int i = 0; i < NUM_DEV; i++) {
		if (work == &projector_invalidators[i]) {
			prj_timestamps[i] = 0;
			k_sem_give(&req_sem[i]);
			break;
		}
	}
}

int pos_srv_init(void)
{
	for (int i = 0; i < NUM_DEV; i++) {
		k_timer_init(&projector_timers[i], timer_handler_prj, NULL);
		k_work_init(&projector_invalidators[i], work_handler_prj);
	}

	return 0;
}

int pos_srv_req(int id, int pos)
{
	if (id < 0 || id >= NUM_DEV) {
		return -EINVAL;
	}

	requests[id] = pos;
	request_timestamps[id] = k_uptime_get();
	k_sem_give(&req_sem[id]);

	return 0;
}

int pos_srv_override(int id, int pos)
{
	if (id < 0 || id >= NUM_DEV) {
		return -EINVAL;
	}

	overridden[id] = pos;
	k_sem_give(&req_sem[id]);

	return 0;
}

int pos_srv_override_release(int id)
{
	if (id < 0 || id >= NUM_DEV) {
		return -EINVAL;
	}

	overridden[id] = MOT_CNT_STOP;
	k_sem_give(&req_sem[id]);

	return 0;
}

int pos_srv_set_projector_state(int id, bool enabled, unsigned long validity_ms)
{
	if (id < 0 || id >= NUM_DEV) {
		return -EINVAL;
	}

	if (enabled) {
		if (!prj_timestamps[id]) {
			prj_timestamps[id] = k_uptime_get();
			k_sem_give(&req_sem[id]);
		}

		k_timer_start(&projector_timers[id], K_MSEC(validity_ms), K_NO_WAIT);
	} else {
		prj_timestamps[id] = 0;
		k_sem_give(&req_sem[id]);
	}

	return 0;
}
