/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Projector state remote notification
 */
    
#ifndef NOTIFICATION_H_
#define NOTIFICATION_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void notification_init(void);
void notification_reset_targets(void);
int notification_add_target(const char *name);
void notification_set_prj_state(bool enabled);

#ifdef __cplusplus
}   
#endif

#endif // NOTIFICATION_H_
