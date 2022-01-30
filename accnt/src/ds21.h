/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Daikin S21 commands enc/dec
 */
    
#ifndef DS21_H_
#define DS21_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ds21_mode {
	DS21_MODE_AUTO = '1',
	DS21_MODE_DRY  = '2',
	DS21_MODE_COOL = '3',
	DS21_MODE_HEAT = '4',
	DS21_MODE_FAN  = '6',
};

enum ds21_fan {
	DS21_FAN_AUTO = 'A',
	DS21_FAN_1    = '3',
	DS21_FAN_2    = '4',
	DS21_FAN_3    = '5',
	DS21_FAN_4    = '6',
	DS21_FAN_5    = '7',
};

struct ds21_basic_state {
	bool enabled;
	enum ds21_mode mode;
	int16_t target_temp; // deciCelsius
	enum ds21_fan fan;
};

void ds21_init(void);

int ds21_get_basic_state(struct ds21_basic_state *state);
int ds21_set_basic_state(const struct ds21_basic_state *state);

#ifdef __cplusplus
}   
#endif

#endif // DS21_H_
