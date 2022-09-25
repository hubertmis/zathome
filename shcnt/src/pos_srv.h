/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>

int pos_srv_init(void);
int pos_srv_req(int id, int pos);
int pos_srv_override(int id, int pos);
int pos_srv_override_release(int id);
int pos_srv_set_projector_state(int id, bool enabled, unsigned long validity_ms);
