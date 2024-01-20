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
#define THREAD_PRIORITY 1
#define DEFAULT_TIME MOT_CNT_DEFAULT_TIME /* ms */

#define STOP_VAL MOT_CNT_STOP
#define MIN_VAL MOT_CNT_MIN
#define MAX_VAL MOT_CNT_MAX

#define RELAY_DELAY 500 /* ms */

enum dir {
	DIR_STOP,
	DIR_DOWN,
	DIR_UP,
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

struct inst {
	struct data *data;
	const struct cfg *cfg;
};

static void start_movement(struct inst *inst, enum dir dir);
static void stop_movement(struct inst *inst);
static void set_dir_up(struct inst *inst);
static void set_dir_down(struct inst *inst);

static int32_t movement_delta(struct inst *inst, int32_t run_time);
static int32_t get_run_time(struct inst *inst, int32_t movement_delta);
static int32_t get_full_run_time(struct inst *inst);
static void save_known_loc(struct inst *inst, int loc);

static int get_curr_pos(struct inst *inst, int64_t *time_output)
{
	struct data *data = inst->data;
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
		/* Time of full swing unknown, cannot calculate relative position. */
		return -EINVAL;
	}

	k_mutex_lock(&data->mutex, K_FOREVER);

	movement_time = curr_time - data->movement_start_time;
	debug_log(11);
	debug_log(movement_time);

	switch (data->dir) {
		case DIR_UP:
			curr_pos = data->known_loc + movement_delta(inst, movement_time);
			debug_log(12);
			debug_log(curr_pos);
			curr_pos = (curr_pos > MAX_VAL) ? MAX_VAL : curr_pos;

			break;

		case DIR_DOWN:
			curr_pos = data->known_loc - movement_delta(inst, movement_time);
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

static void update_curr_pos(struct inst *inst)
{
	struct data *data = inst->data;
	int64_t curr_time = 0;
	int last_known_loc = data->known_loc;
	int curr_loc = get_curr_pos(inst, &curr_time);

	if (curr_loc < 0) {
		/* Current location is unknown. */
		return;
	}

	k_mutex_lock(&data->mutex, K_FOREVER);
	data->known_loc = curr_loc;
	data->movement_start_time = curr_time;

	debug_log(20);
	debug_log(data->movement_start_time);

	if ((last_known_loc != curr_loc) && data->loc_uncert < INT_MAX) {
		data->loc_uncert++;
	}
	k_mutex_unlock(&data->mutex);
}

static void go_stop(struct inst *inst)
{
	switch (inst->data->dir) {
		case DIR_UP:
		case DIR_DOWN:
			debug_log(40);
			stop_movement(inst);
			k_sleep(K_MSEC(RELAY_DELAY));
			set_dir_down(inst);
			k_sleep(K_MSEC(RELAY_DELAY));

			break;

		case DIR_STOP:
			debug_log(41);
			break;
	}
}

static int go_down(struct inst *inst, int32_t run_time)
{
	int ret;
	struct data *data = inst->data;

	if (data->known_loc == MIN_VAL && data->loc_uncert == 0 && data->dir == DIR_STOP) {
		/* Extreme position: We can skip this movement and act like it was requested movement */
		return -EAGAIN;
	}

	switch (data->dir) {
		case DIR_UP:
			debug_log(30);
			stop_movement(inst);
			k_sleep(K_MSEC(RELAY_DELAY));
			set_dir_down(inst);
			k_sleep(K_MSEC(RELAY_DELAY));

			/* fall through */

		case DIR_STOP:
			if (!k_sem_count_get(&data->sem) && (run_time >= RELAY_DELAY)) {
				start_movement(inst, DIR_DOWN);
				k_mutex_lock(&data->mutex, K_FOREVER);
				debug_log(31);
				debug_log(data->movement_start_time);
				k_mutex_unlock(&data->mutex);
				k_sleep(K_MSEC(RELAY_DELAY));

				run_time -= RELAY_DELAY;
			} else {
				/* Semaphore is already given or running time is too short. Skip starting movement */
				/* Direction relay is already in the default position (down). */
				run_time = 1;
				debug_log(32);
			}

			break;

		case DIR_DOWN:
			break;
	}

	ret = k_sem_take(&data->sem, K_MSEC(run_time));

	if (ret == -EAGAIN) {
		/* Timeout */
		debug_log(33);
		stop_movement(inst);
	}

	return ret;
}

static int go_up(struct inst *inst, int32_t run_time)
{
	int ret;
	struct data *data = inst->data;

	if (data->known_loc == MAX_VAL && data->loc_uncert == 0 && data->dir == DIR_STOP) {
		/* Extreme position: We can skip this movement and act like it was requested movement */
		return -EAGAIN;
	}

	switch (data->dir) {
		case DIR_DOWN:
			stop_movement(inst);
			k_sleep(K_MSEC(RELAY_DELAY));

			/* fall through */

		case DIR_STOP:
			set_dir_up(inst);
			k_sleep(K_MSEC(RELAY_DELAY));

			if (!k_sem_count_get(&data->sem) && (run_time >= RELAY_DELAY)) {
				start_movement(inst, DIR_UP);
				k_mutex_lock(&data->mutex, K_FOREVER);
				debug_log(51);
				debug_log(data->movement_start_time);
				k_mutex_unlock(&data->mutex);
				k_sleep(K_MSEC(RELAY_DELAY));

				run_time -= RELAY_DELAY;
			} else {
				/* Semaphore is already given or running time is too short. Skip starting movement */
				/* Set direction relay to the default position. */
				set_dir_down(inst);
				k_sleep(K_MSEC(RELAY_DELAY));
				run_time = 1;
			}

			break;

		case DIR_UP:
			break;
	}

	ret = k_sem_take(&data->sem, K_MSEC(run_time));

	if (ret == -EAGAIN) {
		/* Timeout */
		stop_movement(inst);
		k_sleep(K_MSEC(RELAY_DELAY));
		set_dir_down(inst); // Default direction
	}

	return ret;
}

static int go_min(struct inst *inst)
{
	int32_t run_time = get_full_run_time(inst);
	int ret = go_down(inst, run_time);

	if (ret == -EAGAIN) {
		/* After full movement, not interrupted by other request */
		save_known_loc(inst, MIN_VAL);
	}

	return ret;
}

static int go_max(struct inst *inst)
{
	int32_t run_time = get_full_run_time(inst);
	int ret = go_up(inst, run_time);

	if (ret == -EAGAIN) {
		/* After full movement, not interrupted by other request */
		save_known_loc(inst, MAX_VAL);
	}

	return ret;
}

static int go_target(struct inst *inst, int target)
{
	struct data *data = inst->data;

	if (data->known_loc < 0) {
		/* Current position is unknown */
		return -EINVAL;
	}

	update_curr_pos(inst);
	int known_loc = data->known_loc;

	if (target > known_loc) {
		int32_t movement_delta = target - known_loc;
		int32_t run_time = get_run_time(inst, movement_delta);
		return go_up(inst, run_time);
	} else if (target < known_loc) {
		int32_t movement_delta = known_loc - target;
		int32_t run_time = get_run_time(inst, movement_delta);
		return go_down(inst, run_time);
	} else {
		go_stop(inst);
		return -EAGAIN;
	}
}

static void start_movement(struct inst *inst, enum dir dir)
{
	const struct relay_api *r_api = inst->cfg->sw_dev->api;
	inst->data->movement_start_time = k_uptime_get();
	r_api->on(inst->cfg->sw_dev);
	inst->data->dir = dir;
}

static void stop_movement(struct inst *inst)
{
	const struct relay_api *r_api = inst->cfg->sw_dev->api;
	r_api->off(inst->cfg->sw_dev);
	update_curr_pos(inst);
	inst->data->dir = DIR_STOP;
}

static void set_dir_up(struct inst *inst)
{
	const struct relay_api *r_api = inst->cfg->dir_dev->api;
	r_api->on(inst->cfg->dir_dev);
}

static void set_dir_down(struct inst *inst)
{
	const struct relay_api *r_api = inst->cfg->dir_dev->api;
	r_api->off(inst->cfg->dir_dev);
}

static int32_t movement_delta(struct inst *inst, int32_t run_time)
{
	int32_t full_swing_run_time = inst->data->run_time ? inst->data->run_time : DEFAULT_TIME;
	return run_time * MAX_VAL / full_swing_run_time;
}

static int32_t get_run_time(struct inst *inst, int32_t movement_delta)
{
	int32_t full_swing_run_time = inst->data->run_time ? inst->data->run_time : DEFAULT_TIME;
	return movement_delta * full_swing_run_time / MAX_VAL;
}

static int32_t get_full_run_time(struct inst *inst)
{
	return inst->data->run_time ? inst->data->run_time * 3 / 2 : DEFAULT_TIME;
}

static void save_known_loc(struct inst *inst, int loc)
{
	k_mutex_lock(&inst->data->mutex, K_FOREVER);
	inst->data->known_loc = loc;
	inst->data->loc_uncert = 0;
	k_mutex_unlock(&inst->data->mutex);
}

static void thread_process(void * dev, void *, void *)
{
	const struct device *mot_cnt_dev = (const void *)dev;
	struct inst inst = {
		.data = mot_cnt_dev->data,
		.cfg = mot_cnt_dev->config,
	};
	struct data *data = inst.data;

	k_sem_take(&data->sem, K_FOREVER);

	while (1) {
		switch (data->target) {
			case STOP_VAL:
				go_stop(&inst);
				break;

			case MIN_VAL:
				if (go_min(&inst) == 0) {
					/* Interrupted by new request */
					continue;
				}
				break;

			case MAX_VAL:
				if (go_max(&inst) == 0) {
					/* Interrupted by new request */
					continue;
				}
				break;

			default:
				if (go_target(&inst, data->target) == 0) {
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
	struct inst inst = {
		.data = dev->data,
		.cfg = dev->config,
	};

	if (!inst.data) {
		return -ENODEV;
	}

	// Is get_curr_pos reentrant? Is there a need for a mutex?

	return get_curr_pos(&inst, NULL);
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
