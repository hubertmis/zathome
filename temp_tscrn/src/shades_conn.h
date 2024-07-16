/*
 * Copyright (c) 2024 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Connection to shade controllers
 */
    
#ifndef SHADES_CONN_H_
#define SHADES_CONN_H_

#ifdef __cplusplus
extern "C" {
#endif

enum shades_conn_item {
	SHADES_CONN_ITEM_LIVING_ROOM,
	SHADES_CONN_ITEM_DINING_ROOM_L,
	SHADES_CONN_ITEM_DINING_ROOM_C,
	SHADES_CONN_ITEM_DINING_ROOM_R,
	SHADES_CONN_ITEM_KITCHEN,
	SHADES_CONN_ITEM_BEDROOM,

	SHADES_CONN_ITEM_NUM
};

void shades_conn_init(void);
void shades_conn_enable_polling(int item);
void shades_conn_disable_polling(void);

#ifdef __cplusplus
}   
#endif

#endif // SHADES_CONN_H_
