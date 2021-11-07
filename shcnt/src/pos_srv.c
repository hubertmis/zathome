/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pos_srv.h"

#include <stdbool.h>
#include <sys/util.h>
#include "mot_cnt.h"
#include "mot_cnt_map.h"

#define NUM_DEV DT_NUM_INST_STATUS_OKAY(hubertmis_mot_cnt)

#define POS_SRV_STACK_SIZE 512
#define POS_SRV_THR_PRIO 4

void pos_srv_thread(void *, void *, void *);

K_THREAD_DEFINE(pos_srv_tid_0, POS_SRV_STACK_SIZE,
		pos_srv_thread, (void *)0, NULL, NULL,
		POS_SRV_THR_PRIO, 0, 0);
K_THREAD_DEFINE(pos_srv_tid_1, POS_SRV_STACK_SIZE,
		pos_srv_thread, (void *)1, NULL, NULL,
		POS_SRV_THR_PRIO, 0, 0);

static int requests[NUM_DEV];
static int overridden[NUM_DEV];
static struct k_sem req_sem[NUM_DEV];

void pos_srv_thread(void * id_val, void *, void *)
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
		int ovr_val;
		int req_val;
		int val;

		k_sem_take(&req_sem[id], K_FOREVER);

		ovr_val = overridden[id];
		req_val = requests[id];
		val = ovr_val != MOT_CNT_STOP ? ovr_val : req_val;

		api->go_to(mot_cnt, val);
	}
}

int pos_srv_req(int id, int pos)
{
	if (id < 0 || id >= NUM_DEV) {
		return -EINVAL;
	}

	requests[id] = pos;
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
