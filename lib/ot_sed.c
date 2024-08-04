/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ot_sed.h"

#include <stdatomic.h>

#include <errno.h>
#include <openthread/link.h>

#define POLL_PERIOD_FAST 750
#define POLL_PERIOD_DEFAULT (240 * 1000)

#ifdef CONFIG_OPENTHREAD_MTD

static struct otInstance *s_instance;
static atomic_int fast_poll_cnt;
static atomic_int med_cnt;

void ot_sed_init(struct otInstance *instance)
{
	s_instance = instance;
}

int ot_sed_enter_fast_polling(void)
{
	if (!s_instance) {
		return -EBUSY;
	}

	if (!fast_poll_cnt) {
		// TODO: guard OpenThread's API with a mutex?
		otLinkSetPollPeriod(s_instance, POLL_PERIOD_FAST);
	}

	fast_poll_cnt++;

	return 0;
}

int ot_sed_exit_fast_polling(void)
{
	if (!s_instance) {
		return -EBUSY;
	}

	fast_poll_cnt--;

	if (!fast_poll_cnt) {
		otLinkSetPollPeriod(s_instance, POLL_PERIOD_DEFAULT);
	}

	return 0;
}

int ot_sed_to_med(void)
{
	const otLinkModeConfig config = {
		.mDeviceType = false,
		.mNetworkData = false,
		.mRxOnWhenIdle = true,
	};

	if (!s_instance) {
		return -EBUSY;
	}

	if (!med_cnt) {
		// TODO: guard OpenThread's API with a mutex?
		otThreadSetLinkMode(s_instance, config);
	}

	med_cnt++;

	return 0;
}

int ot_sed_from_med(void)
{
	const otLinkModeConfig config = {
		.mDeviceType = false,
		.mNetworkData = false,
		.mRxOnWhenIdle = false,
	};

	if (!s_instance) {
		return -EBUSY;
	}

	med_cnt--;

	if (!med_cnt) {
		otThreadSetLinkMode(s_instance, config);
	}

	return 0;
}

#endif /* CONFIG_OPENTHREAD_MTD */
