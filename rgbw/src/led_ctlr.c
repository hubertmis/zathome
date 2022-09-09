/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led_ctlr.h"

#include <stdbool.h>

#include <kernel.h>

#define DEACTIVATED_LED (MAX_BRIGHTNESS + 1)
#define AUTO_ANIM_DUR_MS 3000
#define DIMMED_ANIM_DUR_MS 10000

static struct leds_brightness leds_auto;
static struct leds_brightness leds_manual;
static unsigned manual_anim_dur_ms;

// TODO: Instead of the state machine store timestamps of last dimmer request and last manual request
//       Based on the timestamps (which request is the newest, select appropriate mode.
//       If dimmer is re-requested while active, do not update its timestamp.
//       If manual is set to dimmed value (off?), reset manual timestamp.
enum dimmed_state {
	DIMMED_STATE_INACTIVE,
	DIMMED_STATE_ACTIVE,
	DIMMED_STATE_OVERRIDDEN,
};
static enum dimmed_state leds_dimmed;

static void timer_handler_invalidate_manual(struct k_timer *timer_id);
static void work_handler_invalidate_manual(struct k_work *work);
static void timer_handler_dimmer(struct k_timer *timer_id);
static void work_handler_dimmer(struct k_work *work);

K_TIMER_DEFINE(manual_invalidate_timer, timer_handler_invalidate_manual, NULL);
K_WORK_DEFINE(manual_invalidator, work_handler_invalidate_manual);

K_TIMER_DEFINE(dimmer_timer, timer_handler_dimmer, NULL);
K_WORK_DEFINE(dimmer_invalidator, work_handler_dimmer);

static void deactivate_leds(struct leds_brightness *leds)
{
	leds->w = DEACTIVATED_LED;
}

static bool are_leds_active(const struct leds_brightness *leds)
{
	return leds->w < DEACTIVATED_LED;
}

static void disable_leds(struct leds_brightness *leds)
{
	leds->r = 0;
	leds->g = 0;
	leds->b = 0;
	leds->w = 0;
}

static void deactivate_manual(void)
{
	if (leds_dimmed == DIMMED_STATE_OVERRIDDEN) {
		leds_dimmed = DIMMED_STATE_ACTIVE;
	}
	deactivate_leds(&leds_manual);
}

static void process(void)
{
	struct leds_brightness dimmed;
	disable_leds(&dimmed);

	switch (leds_dimmed) {
		case DIMMED_STATE_ACTIVE:
			led_anim(&dimmed, DIMMED_ANIM_DUR_MS);
			break;

		case DIMMED_STATE_OVERRIDDEN:
			// In overridden state manual should be active.
			// But it can be handled together with inactive state with the same result.
		case DIMMED_STATE_INACTIVE:
			if (are_leds_active(&leds_manual)) {
				led_anim(&leds_manual, manual_anim_dur_ms);
			} else {
				led_anim(&leds_auto, AUTO_ANIM_DUR_MS);
			}
			break;
	}
}

static void timer_handler_invalidate_manual(struct k_timer *timer_id)
{
	(void)timer_id;

	k_work_submit(&manual_invalidator);
}

static void work_handler_invalidate_manual(struct k_work *work)
{
	(void)work;

	deactivate_manual();
	process();
}

static void timer_handler_dimmer(struct k_timer *timer_id)
{
	(void)timer_id;
	k_work_submit(&dimmer_invalidator);
}

static void work_handler_dimmer(struct k_work *work)
{
	(void)work;

	leds_dimmed = DIMMED_STATE_INACTIVE;
	process();
}

void led_ctlr_init(void)
{
	leds_dimmed = DIMMED_STATE_INACTIVE;
	disable_leds(&leds_auto);
	disable_leds(&leds_manual);
	deactivate_leds(&leds_manual);
}

int led_ctlr_set_auto(const struct leds_brightness *leds)
{
	leds_auto = *leds;
	process();
	return 0;
}

int led_ctlr_set_manual(const struct leds_brightness *leds, unsigned anim_dur_ms, unsigned long validity_ms)
{
	leds_manual = *leds;
	manual_anim_dur_ms = anim_dur_ms;
	
	if (leds_dimmed == DIMMED_STATE_ACTIVE) {
		leds_dimmed = DIMMED_STATE_OVERRIDDEN;
	}

	process();

	k_timer_start(&manual_invalidate_timer, K_MSEC(validity_ms), K_NO_WAIT);
	return 0;
}

int led_ctlr_reset_manual(void)
{
	deactivate_manual();
	process();
	return 0;
}

int led_ctlr_dim(unsigned long validity_ms)
{
	if (leds_dimmed == DIMMED_STATE_INACTIVE) {
		leds_dimmed = DIMMED_STATE_ACTIVE;
	}

	process();

	k_timer_start(&dimmer_timer, K_MSEC(validity_ms), K_NO_WAIT);
	return 0;
}

int led_ctlr_reset_dimmer(void)
{
	leds_dimmed = DIMMED_STATE_INACTIVE;
	process();
	return 0;
}
