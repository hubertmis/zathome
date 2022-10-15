/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hubertmis_mot_cnt

#include "mot_cnt.h"

#include <relay.h>

#include "debug_log.h"

#define THREAD_STACK_SIZE 1024
#define THREAD_PRIORITY 0
#define DEFAULT_TIME MOT_CNT_DEFAULT_TIME /* ms */

#define STOP_VAL MOT_CNT_STOP
#define MIN_VAL MOT_CNT_MIN
#define MAX_VAL MOT_CNT_MAX

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
	struct k_mutex mutex;

	int target;
	enum dir dir;
	uint32_t run_time;

	int64_t movement_start_time;
	int known_loc;
	int loc_uncert;
};

struct cfg {
	const struct device *sw_dev;
	const struct device *dir_dev;
};

static int get_curr_pos(struct data *data, int64_t *time_output)
{
	int64_t curr_time = k_uptime_get();
	int64_t movement_time;
	int curr_pos;

	debug_log(10);
	debug_log((uint32_t)curr_time);

	if (data->known_loc < 0) {
		/* Last position was unknown, cannot calculate current. */
		return -EINVAL;
	}
	if (!data->run_time) {
		/* Time of total run unknown, cannot calculate relative position. */
		return -EINVAL;
	}

	k_mutex_lock(&data->mutex, K_FOREVER);

	movement_time = curr_time - data->movement_start_time;
	debug_log(11);
	debug_log(movement_time);

	switch (data->dir) {
		case DIR_INC:
			curr_pos = data->known_loc + movement_time * MAX_VAL / data->run_time;
			debug_log(12);
			debug_log(curr_pos);
			curr_pos = (curr_pos > MAX_VAL) ? MAX_VAL : curr_pos;

			break;

		case DIR_DEC:
			curr_pos = data->known_loc - movement_time * MAX_VAL / data->run_time;
			debug_log(13);
			debug_log(curr_pos);
			curr_pos = (curr_pos < MIN_VAL) ? MIN_VAL : curr_pos;

			break;

		case DIR_STOP:
			curr_pos = data->known_loc;
			debug_log(14);
			debug_log(curr_pos);
			break;

		default:
			/* Invalid direction. Return error. */
			curr_pos = -EINVAL;
	}

	k_mutex_unlock(&data->mutex);

	debug_log(15);
	if (time_output && curr_pos >= 0) {
		debug_log(16);
		debug_log(curr_time);
		*time_output = curr_time;
	}

	return curr_pos;
}

static void update_curr_pos(struct data *data)
{
	int64_t curr_time = 0;
	int curr_pos = get_curr_pos(data, &curr_time);

	if (curr_pos < 0) {
		/* Current position is unknown. */
		return;
	}

	k_mutex_lock(&data->mutex, K_FOREVER);
	data->known_loc = curr_pos;
	data->movement_start_time = curr_time;

	debug_log(20);
	debug_log(data->movement_start_time);

	if (data->loc_uncert < INT_MAX) {
		data->loc_uncert++;
	}
	k_mutex_unlock(&data->mutex);
}

static void go_stop(struct data *data, const struct cfg *cfg)
{
	const struct relay_api *r_api = cfg->sw_dev->api;

	switch (data->dir) {
		case DIR_INC:
		case DIR_DEC:
			debug_log(40);
			r_api->off(cfg->sw_dev);
			update_curr_pos(data);
			k_sleep(K_MSEC(RELAY_DELAY));
			r_api->off(cfg->dir_dev);

			break;

		case DIR_STOP:
			debug_log(41);
			break;
	}

	data->dir = DIR_STOP;
}

static int go_down(struct data *data, const struct cfg *cfg, int32_t run_time)
{
	int ret;
	const struct relay_api *r_api = cfg->sw_dev->api;

	if (data->known_loc == MIN_VAL && data->loc_uncert == 0 && data->dir == DIR_STOP) {
		/* Extreme position: We can skip this movement and act like it was requested movement */
		return -EAGAIN;
	}

	switch (data->dir) {
		case DIR_INC:
			debug_log(30);
			r_api->off(cfg->sw_dev);
			update_curr_pos(data);
			k_sleep(K_MSEC(RELAY_DELAY));
			r_api->off(cfg->dir_dev);
			k_sleep(K_MSEC(RELAY_DELAY));

			/* fall through */

		case DIR_STOP:
			if (!k_sem_count_get(&data->sem) && (run_time >= RELAY_DELAY)) {
				k_mutex_lock(&data->mutex, K_FOREVER);
				data->movement_start_time = k_uptime_get();
				debug_log(31);
				debug_log(data->movement_start_time);
				k_mutex_unlock(&data->mutex);
				r_api->on(cfg->sw_dev);
				k_sleep(K_MSEC(RELAY_DELAY));
				data->dir = DIR_DEC;

				run_time -= RELAY_DELAY;
			} else {
				/* Semaphore is already given or running time is too short. Skip starting movement */
				data->dir = DIR_STOP;

				run_time = 1;
				debug_log(32);
			}

			break;

		case DIR_DEC:
			break;
	}

	ret = k_sem_take(&data->sem, K_MSEC(run_time));

	if (ret == -EAGAIN) {
		/* Timeout */
		debug_log(33);
		r_api->off(cfg->sw_dev);
		update_curr_pos(data);
		data->dir = DIR_STOP;
	}

	return ret;
}

static int go_up(struct data *data, const struct cfg *cfg, int32_t run_time)
{
	int ret;
	const struct relay_api *r_api = cfg->sw_dev->api;

	if (data->known_loc == MAX_VAL && data->loc_uncert == 0 && data->dir == DIR_STOP) {
		/* Extreme position: We can skip this movement and act like it was requested movement */
		return -EAGAIN;
	}

	switch (data->dir) {
		case DIR_DEC:
			r_api->off(cfg->sw_dev);
			update_curr_pos(data);
			k_sleep(K_MSEC(RELAY_DELAY));

			/* fall through */

		case DIR_STOP:
			r_api->on(cfg->dir_dev);
			k_sleep(K_MSEC(RELAY_DELAY));

			if (!k_sem_count_get(&data->sem) && (run_time >= RELAY_DELAY)) {
				k_mutex_lock(&data->mutex, K_FOREVER);
				data->movement_start_time = k_uptime_get();
				debug_log(51);
				debug_log(data->movement_start_time);
				k_mutex_unlock(&data->mutex);
				r_api->on(cfg->sw_dev);
				k_sleep(K_MSEC(RELAY_DELAY));
				data->dir = DIR_INC;

				run_time -= RELAY_DELAY;
			} else {
				/* Semaphore is already given or running time is too short. Skip starting movement */
				r_api->off(cfg->dir_dev);
				data->dir = DIR_STOP;

				run_time = 1;
			}

			break;

		case DIR_INC:
			break;
	}

	ret = k_sem_take(&data->sem, K_MSEC(run_time));

	if (ret == -EAGAIN) {
		/* Timeout */
		r_api->off(cfg->sw_dev);
		update_curr_pos(data);
		k_sleep(K_MSEC(RELAY_DELAY));
		r_api->off(cfg->dir_dev);
		data->dir = DIR_STOP;
	}

	return ret;
}

static int go_min(struct data *data, const struct cfg *cfg)
{
	int32_t run_time = data->run_time ? data->run_time * 3 / 2 : DEFAULT_TIME;
	int ret = go_down(data, cfg, run_time);

	if (ret == -EAGAIN) {
		/* After full movement, not interrupted by other request */
		k_mutex_lock(&data->mutex, K_FOREVER);
		data->known_loc = MIN_VAL;
		data->loc_uncert = 0;
		k_mutex_unlock(&data->mutex);
	}

	return ret;
}

static int go_max(struct data *data, const struct cfg *cfg)
{
	int32_t run_time = data->run_time ? data->run_time * 3 / 2 : DEFAULT_TIME;
	int ret = go_up(data, cfg, run_time);

	if (ret == -EAGAIN) {
		/* After full movement, not interrupted by other request */
		k_mutex_lock(&data->mutex, K_FOREVER);
		data->known_loc = MAX_VAL;
		data->loc_uncert = 0;
		k_mutex_unlock(&data->mutex);
	}

	return ret;
}

static int go_target(struct data *data, const struct cfg *cfg)
{
	int32_t run_time = data->run_time ? data->run_time : DEFAULT_TIME;

	if (data->known_loc < 0) {
		/* Current position is unknown */
		return -EINVAL;
	}

	update_curr_pos(data);

	k_mutex_lock(&data->mutex, K_FOREVER);
	int target = data->target;
	int known_loc = data->known_loc;
	k_mutex_unlock(&data->mutex);

	if (target > known_loc) {
		return go_up(data, cfg, (target - known_loc) * run_time / MAX_VAL);
	} else if (target < known_loc) {
		return go_down(data, cfg, (known_loc - target) * run_time / MAX_VAL);
	} else {
		go_stop(data, cfg);
		return 0;
	}
}

static void thread_process(void * dev, void *, void *)
{
	const struct device *mot_cnt_dev = (const void *)dev;
	struct data *data = mot_cnt_dev->data;
	const struct cfg *cfg = mot_cnt_dev->config;

	k_sem_take(&data->sem, K_FOREVER);

	while (1) {
		switch (data->target) {
			case STOP_VAL:
				go_stop(data, cfg);
				break;

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

			default:
				if (go_target(data, cfg) == 0) {
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
	data->run_time = 0;
	data->known_loc = -1;
	data->movement_start_time = 0;
	data->loc_uncert = 0;

	k_sem_init(&data->sem, 0, 1);
	k_mutex_init(&data->mutex);

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

static int stop(const struct device *dev)
{
	struct data *data = dev->data;

	if (!data) {
		return -ENODEV;
	}

	data->target = STOP_VAL;
	k_sem_give(&data->sem);

	return 0;
}

static int set_run_time(const struct device *dev, uint32_t run_time_ms)
{
	struct data *data = dev->data;

	if (!data) {
		return -ENODEV;
	}

	data->run_time = run_time_ms;

	return 0;
}

static int go_to(const struct device *dev, uint32_t target)
{
	struct data *data = dev->data;

	if (!data) {
		return -ENODEV;
	}

	data->target = target;
	k_sem_give(&data->sem);

	return 0;
}

static int get_pos(const struct device *dev)
{
	struct data *data = dev->data;

	if (!data) {
		return -ENODEV;
	}

	// Is get_curr_pos reentrant? Is there a need for a mutex?

	return get_curr_pos(data, NULL);
}

static const struct mot_cnt_api mot_cnt_api = {
	.min = min,
	.max = max,
	.stop = stop,
	.set_run_time = set_run_time,
	.go_to = go_to,
	.get_pos = get_pos,
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
