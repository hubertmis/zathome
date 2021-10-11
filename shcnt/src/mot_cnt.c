/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hubertmis_mot_cnt

#include "mot_cnt.h"

#include <relay.h>

#define THREAD_STACK_SIZE 1024
#define THREAD_PRIORITY 0
#define DEFAULT_TIME 90000 /* ms */

#define MIN_VAL 0
#define MAX_VAL 255

#define RELAY_DELAY 500 /* ms */

enum dir {
	DIR_STOP,
	DIR_DEC,
	DIR_INC,
};

struct data {
	K_KERNEL_STACK_MEMBER(thread_stack, THREAD_STACK_SIZE);
	struct k_thread thread_data;
	k_tid_t thread_id;

	struct k_sem sem;

	enum dir dir;
	uint8_t target;
};

struct cfg {
	const struct device *sw_dev;
	const struct device *dir_dev;
};

static int go_min(struct data *data, const struct cfg *cfg)
{
	int ret;
	const struct relay_api *r_api = cfg->sw_dev->api;

	switch (data->dir) {
		case DIR_INC:
			r_api->off(cfg->sw_dev);
			k_sleep(K_MSEC(RELAY_DELAY));

			/* fall through */

		case DIR_STOP:
			r_api->off(cfg->dir_dev);
			k_sleep(K_MSEC(RELAY_DELAY));

			if (!k_sem_count_get(&data->sem)) {
				r_api->on(cfg->sw_dev);
				k_sleep(K_MSEC(RELAY_DELAY));
				data->dir = DIR_DEC;
			} else {
				data->dir = DIR_STOP;
			}

			break;

		case DIR_DEC:
			break;
	}


	ret = k_sem_take(&data->sem, K_MSEC(DEFAULT_TIME));

	if (ret == -EAGAIN) {
		/* Timeout */
		r_api->off(cfg->sw_dev);
		data->dir = DIR_STOP;
	}

	return ret;
}

static int go_max(struct data *data, const struct cfg *cfg)
{
	int ret;
	const struct relay_api *r_api = cfg->sw_dev->api;

	switch (data->dir) {
		case DIR_DEC:
			r_api->off(cfg->sw_dev);
			k_sleep(K_MSEC(RELAY_DELAY));

			/* fall through */

		case DIR_STOP:
			r_api->on(cfg->dir_dev);
			k_sleep(K_MSEC(RELAY_DELAY));

			if (!k_sem_count_get(&data->sem)) {
				r_api->on(cfg->sw_dev);
				k_sleep(K_MSEC(RELAY_DELAY));
				data->dir = DIR_INC;
			} else {
				r_api->off(cfg->dir_dev);
				data->dir = DIR_STOP;
			}

			break;

		case DIR_INC:
			break;
	}

	ret = k_sem_take(&data->sem, K_MSEC(DEFAULT_TIME));

	if (ret == -EAGAIN) {
		/* Timeout */
		r_api->off(cfg->sw_dev);
		k_sleep(K_MSEC(RELAY_DELAY));
		r_api->off(cfg->dir_dev);
		data->dir = DIR_STOP;
	}

	return ret;
}

static void thread_process(void * dev, void *, void *)
{
	const struct device *mot_cnt_dev = (const void *)dev;
	struct data *data = mot_cnt_dev->data;
	const struct cfg *cfg = mot_cnt_dev->config;

	k_sem_take(&data->sem, K_FOREVER);

	while (1) {
		switch (data->target) {
			case MIN_VAL:
				if (go_min(data, cfg) == 0) {
					/* Interrupted by new request */
					continue;
				}
				break;

			case MAX_VAL:
				if (go_max(data, cfg) == 0) {
					/* Interrupted by new request */
					continue;
				}
				break;
		}

		k_sem_take(&data->sem, K_FOREVER);
	}
}

static int init_mot_cnt(const struct device *dev)
{
	struct data *data = dev->data;

	if (!data) {
		return -ENODEV;
	}

	data->dir = DIR_STOP;
	data->target = 0;
	k_sem_init(&data->sem, 0, 1);

	data->thread_id = k_thread_create(&data->thread_data,
		                          data->thread_stack,
					  K_THREAD_STACK_SIZEOF(data->thread_stack),
					  thread_process,
					  (void*)dev, NULL, NULL,
					  THREAD_PRIORITY,
					  0,
					  K_NO_WAIT);

	return 0;
}

static int min(const struct device *dev)
{
	struct data *data = dev->data;

	if (!data) {
		return -ENODEV;
	}

	data->target = MIN_VAL;
	k_sem_give(&data->sem);

	return 0;
}

static int max(const struct device *dev)
{
	struct data *data = dev->data;

	if (!data) {
		return -ENODEV;
	}

	data->target = MAX_VAL;
	k_sem_give(&data->sem);

	return 0;
}

static const struct mot_cnt_api mot_cnt_api = {
	.min = min,
	.max = max,
};

#define MOT_CNT_DEV_DEFINE(inst) \
	static const struct cfg cfg##inst = {\
		.sw_dev = DEVICE_DT_GET(DT_PHANDLE(DT_DRV_INST(inst), sw)), \
		.dir_dev = DEVICE_DT_GET(DT_PHANDLE(DT_DRV_INST(inst), dir)), \
	};                                   \
	static struct data data##inst;       \
	DEVICE_DT_DEFINE(DT_DRV_INST(inst),  \
			init_mot_cnt,        \
			NULL,                \
			&data##inst,         \
			&cfg##inst,          \
			POST_KERNEL, 50,     \
			&mot_cnt_api         \
			);

DT_INST_FOREACH_STATUS_OKAY(MOT_CNT_DEV_DEFINE)
