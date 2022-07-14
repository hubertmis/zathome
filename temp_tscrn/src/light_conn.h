/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Connection to RGBW leds system
 */
    
#ifndef LIGHT_CONN_H_
#define LIGHT_CONN_H_

#ifdef __cplusplus
extern "C" {
#endif

enum light_conn_item {
	LIGHT_CONN_ITEM_BEDROOM_BED,
	LIGHT_CONN_ITEM_BEDROOM_WARDROBE,
	LIGHT_CONN_ITEM_LIVINGROOM,
	LIGHT_CONN_ITEM_DININGROOM,

	LIGHT_CONN_ITEM_NUM
};

void light_conn_init(void);
void light_conn_enable_polling(int item);
void light_conn_disable_polling(void);

#ifdef __cplusplus
}   
#endif

#endif // LIGHT_CONN_H_

