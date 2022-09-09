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

static struct otInstance *s_instance;
static atomic_int fast_poll_cnt; // TODO: atomic

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
