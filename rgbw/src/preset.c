/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "preset.h"

#include "led.h"
#include "prov.h"

#include <errno.h>

static int load_preset(unsigned preset,
		 leds_brightness *leds)
{
	int ret;
	struct prov_leds_brightness stored_preset;

	if (preset >= PROV_NUM_PRESETS) {
		return -ENOENT;
	}

	ret = prov_get_preset(preset, &stored_preset);
	if (ret < 0) {
		return ret;
	}

	*leds = stored_preset;
	return 0;
}

int preset_get(unsigned preset,
		leds_brightness *leds,
		unsigned *duration)
{
	if (preset) {
		int ret;
		ret = load_preset(preset, leds);

		if (ret < 0) {
			return ret;
		}

		*duration = 1000;
		return 0;
	} else {
		leds_brightness leds0;
		led_get(&leds0);

		if (leds0.r || leds0.g || leds0.b || leds0.w) {
			// Something is enabled. Swtich leds off.
			leds->r = 0;
			leds->g = 0;
			leds->b = 0;
			leds->w = 0;
		} else {
			// LEDs are switched off. Switch them on using preset 0.
			int ret;
			ret = load_preset(0, leds);

			if (ret < 0) {
				return ret;
			}
		}
		*duration = 2000;

		return 0;
	}
}
