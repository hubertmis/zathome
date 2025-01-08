/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "data_dispatcher.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define DEFAULT_TEMP 200
#define DEFAULT_HYST 5
#define DEFAULT_P    3584
#define DEFAULT_I    255

static data_dispatcher_publish_t   data_store[DATA_NUM][DATA_LOC_NUM];
static data_dispatcher_subscribe_t *subscribers[DATA_NUM];

static void set_default_data(void)
{
    for (int i = 0; i < DATA_NUM; i++)
    {
        for (int j = 0; j < DATA_LOC_NUM; j++)
        {
            data_store[i][j].type = i;
            data_store[i][j].loc  = j;
        }
    }

    for (int j = 0; j < DATA_LOC_NUM; j++)
    {
        data_store[DATA_TEMP_MEASUREMENT][j].temp_measurement = DEFAULT_TEMP;
        data_store[DATA_TEMP_SETTING][j].temp_setting         = DEFAULT_TEMP;

        for (int k = 0; k < DATA_SHADE_ID_NUM; k++) {
            data_store[DATA_SHADES_CURR][j].shades_curr.values[k] = DATA_SHADES_VAL_UNKNOWN;
        }
    }

    data_store[DATA_CONTROLLER][DATA_LOC_LOCAL].controller.mode  = DATA_CTLR_PID;
    data_store[DATA_CONTROLLER][DATA_LOC_LOCAL].controller.p     = DEFAULT_P;
    data_store[DATA_CONTROLLER][DATA_LOC_LOCAL].controller.i     = DEFAULT_I;
    data_store[DATA_CONTROLLER][DATA_LOC_REMOTE].controller.mode = DATA_CTLR_PID;
    data_store[DATA_CONTROLLER][DATA_LOC_REMOTE].controller.p    = DEFAULT_P;
    data_store[DATA_CONTROLLER][DATA_LOC_REMOTE].controller.i    = DEFAULT_I;

}

void data_dispatcher_init(void)
{
    for (int i = 0; i < DATA_NUM; i++)
    {
        subscribers[i] = NULL;
    }

    memset(data_store, 0, sizeof(data_store));

    set_default_data();
}

void data_dispatcher_subscribe(data_t type, data_dispatcher_subscribe_t *subscribe)
{
    assert(type < DATA_NUM);
    assert(subscribe != NULL);

    subscribe->next   = subscribers[type];
    subscribers[type] = subscribe;
}

void data_dispatcher_unsubscribe(data_t type, data_dispatcher_subscribe_t *subscribe)
{
    assert(type < DATA_NUM);
    assert(subscribe != NULL);

    for (data_dispatcher_subscribe_t *item = subscribers[type]; item != NULL; item = item->next)
    {
        if (item->next == subscribe)
        {
            item->next = subscribe->next;
        }
    }

    if (subscribers[type] == subscribe)
    {
        subscribers[type] = subscribe->next;
    }
}

void data_dispatcher_publish(data_dispatcher_publish_t *data)
{
    assert(data != NULL);

    data_loc_t loc  = data->loc;
    data_t     type = data->type;

    assert(loc < DATA_LOC_NUM);
    assert(type < DATA_NUM);

    data_dispatcher_publish_t *entry = &data_store[type][loc];
    *entry = *data;

    for (data_dispatcher_subscribe_t *item = subscribers[type]; item != NULL; item = item->next)
    {
        item->callback(data);
    }
}

void data_dispatcher_get(data_t type, data_loc_t loc, const data_dispatcher_publish_t **data)
{
    assert(type < DATA_NUM);
    assert(loc < DATA_LOC_NUM);

    *data = &data_store[type][loc];
}
