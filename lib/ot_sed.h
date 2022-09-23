/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Sleepy End Device management
 */
    
#ifndef OT_SED_H_
#define OT_SED_H_

#include <openthread/thread.h>

#ifdef __cplusplus
extern "C" {
#endif

void ot_sed_init(struct otInstance *instance);

#ifdef CONFIG_OPENTHREAD_MTD
int ot_sed_enter_fast_polling(void);
int ot_sed_exit_fast_polling(void);
int ot_sed_to_med(void);
int ot_sed_from_med(void);
#else
static inline int ot_sed_enter_fast_polling(void) { return 0; }
static inline int ot_sed_exit_fast_polling(void) { return 0; }
#endif

#ifdef __cplusplus
}   
#endif

#endif // OT_SED_H_
