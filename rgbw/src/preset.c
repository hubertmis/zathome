/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "preset.h"

#include "led.h"

#include <errno.h>

int preset_get(unsigned preset,
		unsigned *r,
		unsigned *g,
		unsigned *b,
		unsigned *w)
{
	switch (preset) {
		case 0:
		{
			int r0, g0, b0, w0;
			led_get(&r0, &g0, &b0, &w0);

			if (r0 || g0 || b0 || w0) {
				// Something is enabled. Swtich leds off.
				*r = 0;
				*g = 0;
				*b = 0;
				*w = 0;
			} else {
				// LEDs are switched off. Switch them on.
				*r = 0;
				*g = 0;
				*b = 0;
				*w = 100;
			}

			return 0;
		}

		case 1:
			*r = 0;
			*g = 0;
			*b = 0;
			*w = 2;
			return 0;

		case 2:
			*r = 0;
			*g = 0;
			*b = 0;
			*w = 30;
			return 0;

		default:
			return -ENOENT;
	}
}
