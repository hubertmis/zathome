/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "continuous_sd.h"

#include "coap_sd.h"

#include "../temp_tscrn/src/display.h"

#define MIN_SD_INTERVAL (1000UL * 10UL)
#define MAX_SD_INTERVAL (1000UL * 60UL * 10UL)

#define TO_INTERVAL (1000UL * 60UL * 31UL)

#ifdef CONFIG_CONTINUOUS_SD_MAX_NUM_RSRCS
#define NUM_ENTRIES CONFIG_CONTINUOUS_SD_MAX_NUM_RSRCS
#else
#define NUM_ENTRIES 2
#endif

K_MUTEX_DEFINE(entries_mutex);

#define CONT_SD_STACK_SIZE 2048
#define CONT_SD_PRIORITY 5

K_SEM_DEFINE(wait_sem, 0, 1);

struct continuous_sd_entry {
    const char *name;
    const char *type;
    bool mesh;
    struct in6_addr addr;

    int sd_missed;
    int64_t last_req_timestamp;
    int64_t last_rsp_timestamp;
};

static struct continuous_sd_entry entries[NUM_ENTRIES];

enum thread_state {
    STATE_IDLE,
    STATE_TIMEOUT,
    STATE_DISCOVER,
};

static struct {
    struct continuous_sd_entry *entry;
    int64_t target_timestamp;
    enum thread_state thread_state;
    int last_sem_take_result;
} current_state;

static bool entry_is_free(struct continuous_sd_entry *entry)
{
    return entry->name == NULL && entry->type == NULL;
}

static struct continuous_sd_entry *entry_find(const char *name, const char *type)
{
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        struct continuous_sd_entry *entry = &entries[i];

        if (name == NULL && entry->name != NULL) continue;
        if (name != NULL && entry->name == NULL) continue;
        if (name != NULL && entry->name != NULL && strcmp(name, entry->name) != 0) continue;

        if (type == NULL && entry->type != NULL) continue;
        if (type != NULL && entry->type == NULL) continue;
        if (type != NULL && entry->type != NULL && strcmp(type, entry->type) != 0) continue;

        return entry;
    }

    return NULL;
}

static int64_t get_timeout_timestamp_for_entry(struct continuous_sd_entry *entry)
{
    if (!entry->last_rsp_timestamp ||                 // No response yet or
        net_ipv6_is_addr_unspecified(&entry->addr)) { // already timed out
        // There is no timeout
        return INT64_MAX;
    }

    return entry->last_rsp_timestamp + TO_INTERVAL;
}

static int64_t get_next_timeout_timestamp(struct continuous_sd_entry **next_timeout_entry)
{
    int64_t timestamp = INT64_MAX;
    struct continuous_sd_entry *next_entry = NULL;

    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        struct continuous_sd_entry *entry = &entries[i];

        if (entry_is_free(entry)) {
            continue;
        }

        int64_t entry_timeout_timestamp = get_timeout_timestamp_for_entry(entry);

        if (entry_timeout_timestamp < timestamp) {
            timestamp = entry_timeout_timestamp;
            next_entry = entry;
        }
    }

    if (next_timeout_entry != NULL) {
        *next_timeout_entry = next_entry;
    }

    return timestamp;
}

static int64_t get_next_retry_timestamp_for_entry(struct continuous_sd_entry *entry)
{
    if (!entry->last_req_timestamp) {
        // Not requested yet. Retry immediately
        return 0;
    }

    int wait_ms = entry->sd_missed > 0 ? entry->sd_missed * MIN_SD_INTERVAL : MAX_SD_INTERVAL;
    if (wait_ms > MAX_SD_INTERVAL) {
        wait_ms = MAX_SD_INTERVAL;
        entry->sd_missed--;
    }

    return entry->last_req_timestamp + wait_ms;
}

static int64_t get_next_retry_timestamp(struct continuous_sd_entry **next_retry_entry)
{
    int64_t timestamp = INT64_MAX;
    struct continuous_sd_entry *next_entry = NULL;

    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        struct continuous_sd_entry *entry = &entries[i];

        if (entry_is_free(entry)) {
            continue;
        }

        int64_t entry_retry_timestamp = get_next_retry_timestamp_for_entry(entry);

        if (entry_retry_timestamp < timestamp) {
            timestamp = entry_retry_timestamp;
            next_entry = entry;
        }
    }

    if (next_retry_entry != NULL) {
        *next_retry_entry = next_entry;
    }

    return timestamp;
}

static void service_found(const struct sockaddr *src_addr, const socklen_t *addrlen,
                          const char *name, const char *type)
{
    const struct sockaddr_in6 *addr_in6;
    struct continuous_sd_entry *entry;

    k_mutex_lock(&entries_mutex, K_FOREVER);
    entry = entry_find(name, type);
    if (entry == NULL) {
        entry = entry_find(name, NULL);
    }
    // TODO: name might be omitted in the registry?

    if (entry == NULL) {
        goto exit;
    }
    if (src_addr->sa_family != AF_INET6) {
        goto exit;
    }

    addr_in6 = (const struct sockaddr_in6 *)src_addr;

    // Restart timeout timer
    entry->last_rsp_timestamp = k_uptime_get();

    memcpy(&entry->addr, &addr_in6->sin6_addr, sizeof(entry->addr));
    entry->sd_missed = 0;

    k_sem_give(&wait_sem);

exit:
    k_mutex_unlock(&entries_mutex);
}

static void sd_thread_process(void *a1, void *a2, void *a3)
{
    (void)a1;
    (void)a2;
    (void)a3;

    int ret;

    while (1) {
        k_mutex_lock(&entries_mutex, K_FOREVER);
        struct continuous_sd_entry *next_retry_entry;
        int64_t next_retry = get_next_retry_timestamp(&next_retry_entry);

        struct continuous_sd_entry *next_timeout_entry;
        int64_t next_timeout = get_next_timeout_timestamp(&next_timeout_entry);
        k_mutex_unlock(&entries_mutex);

        if (next_timeout < next_retry) {
            // Next action is timeout
            current_state.thread_state = STATE_TIMEOUT;
            current_state.entry = next_timeout_entry;
            current_state.target_timestamp = next_timeout;
            ret = k_sem_take(&wait_sem, K_TIMEOUT_ABS_MS(next_timeout));
            current_state.last_sem_take_result = ret;
            if (ret != -EAGAIN) {
                // Waiting preempted by semaphore. Check again what to do
                continue;
            }

            k_mutex_lock(&entries_mutex, K_FOREVER);
            memcpy(&next_timeout_entry->addr, net_ipv6_unspecified_address(), sizeof(next_timeout_entry->addr));
            k_mutex_unlock(&entries_mutex);
        } else if (next_retry < INT64_MAX) {
            // Next action is retry
            current_state.thread_state = STATE_DISCOVER;
            current_state.entry = next_retry_entry;
            current_state.target_timestamp = next_retry;
            ret = k_sem_take(&wait_sem, K_TIMEOUT_ABS_MS(next_retry));
            current_state.last_sem_take_result = ret;
            if (ret != -EAGAIN) {
                // Waiting preempted by semaphore. Check again what to do
                continue;
            }

            k_mutex_lock(&entries_mutex, K_FOREVER);
            const char *name = next_retry_entry->name;
            const char *type = next_retry_entry->type;
            bool mesh = next_retry_entry->mesh;
            next_retry_entry->sd_missed++; // Increment up front. service_found() would eventually clear it.
            next_retry_entry->last_req_timestamp = k_uptime_get();
            k_mutex_unlock(&entries_mutex);

            (void)coap_sd_start(name, type, service_found, mesh);
        } else {
            // There is no action to perform
            current_state.thread_state = STATE_IDLE;
            current_state.entry = NULL;
            current_state.target_timestamp = -1;
            k_sem_take(&wait_sem, K_FOREVER);
            current_state.last_sem_take_result = 1;
        }
    }
}

K_THREAD_DEFINE(cont_sd_tid, CONT_SD_STACK_SIZE, sd_thread_process,
                NULL, NULL, NULL,
                CONT_SD_PRIORITY, 0, 0);

int continuous_sd_register(const char *name, const char *type, bool mesh)
{
    int r = 0;
    k_mutex_lock(&entries_mutex, K_FOREVER);

    struct continuous_sd_entry *entry = entry_find(name, type);
    if (entry != NULL) {
        r = -EALREADY;
        goto exit;
    }

    entry = entry_find(NULL, NULL);
    if (entry == NULL) {
       r = -ENOMEM;
       goto exit;
    }

    entry->mesh = mesh;
    memcpy(&entry->addr, net_ipv6_unspecified_address(), sizeof(entry->addr));
    entry->sd_missed = 0;
    entry->last_req_timestamp = 0;
    entry->last_rsp_timestamp = 0;
    entry->name = name;
    entry->type = type;

    k_sem_give(&wait_sem);

exit:
    k_mutex_unlock(&entries_mutex);
    return r;
}

int continuous_sd_unregister(const char *name, const char *type)
{
    int r = 0;
    k_mutex_lock(&entries_mutex, K_FOREVER);

    struct continuous_sd_entry *entry = entry_find(name, type);
    if (entry == NULL) {
       r = -ENOENT;
       goto exit;
    }

    entry->name = NULL;
    entry->type = NULL;
    memcpy(&entry->addr, net_ipv6_unspecified_address(), sizeof(entry->addr));
    entry->sd_missed = 0;
    entry->last_req_timestamp = 0;
    entry->last_rsp_timestamp = 0;

    k_sem_give(&wait_sem);

exit:
    k_mutex_unlock(&entries_mutex);
    return r;
}

int continuous_sd_unregister_all(void)
{
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        struct continuous_sd_entry *entry = &entries[i];

        k_mutex_lock(&entries_mutex, K_FOREVER);
        entry->name = NULL;
        entry->type = NULL;
        memcpy(&entry->addr, net_ipv6_unspecified_address(), sizeof(entry->addr));
        entry->sd_missed = 0;
        entry->last_req_timestamp = 0;
        entry->last_rsp_timestamp = 0;
        k_mutex_unlock(&entries_mutex);
    }

    k_sem_give(&wait_sem);

    return 0;
}

int continuous_sd_get_addr(const char *name, const char *type, struct in6_addr *addr)
{
    int r = 0;
    k_mutex_lock(&entries_mutex, K_FOREVER);

    struct continuous_sd_entry *entry = entry_find(name, type);
    if (entry == NULL) {
       r = -ENOENT;
       goto exit;
    }

    if (net_ipv6_is_addr_unspecified(&entry->addr)) {
        r = -ENXIO;
        goto exit;
    }

    memcpy(addr, &entry->addr, sizeof(*addr));

exit:
    k_mutex_unlock(&entries_mutex);
    return r;
}

int continuous_sd_get_any_addr(struct in6_addr *addr)
{
    if (addr == NULL) {
        return -EINVAL;
    }

    int r = -ENOENT;
    k_mutex_lock(&entries_mutex, K_FOREVER);

    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        struct continuous_sd_entry *entry = &entries[i];

        if (entry_is_free(entry)) {
            continue;
        }

        if (net_ipv6_is_addr_unspecified(&entry->addr)) {
            continue;
        }

        memcpy(addr, &entry->addr, sizeof(*addr));
        r = 0;
        break;
    }

    k_mutex_unlock(&entries_mutex);
    return r;
}

void continuous_sd_debug(int *state, int64_t *target_time,
        const char **name, const char **type, int *sd_missed,
        int64_t *last_req_ts, int64_t *last_rsp_ts,
        int *last_sem_take_result,
        k_ticks_t *remaining_thread_ticks)
{
    *state = current_state.thread_state;
    *target_time = current_state.target_timestamp;
    *last_sem_take_result = current_state.last_sem_take_result;
    *remaining_thread_ticks = k_thread_timeout_remaining_ticks(cont_sd_tid);

    if (current_state.entry) {
        *name = current_state.entry->name;
        *type = current_state.entry->type;
        *sd_missed = current_state.entry->sd_missed;
        *last_req_ts = current_state.entry->last_req_timestamp;
        *last_rsp_ts = current_state.entry->last_rsp_timestamp;;
    } else {
        *name = "";
        *type = "";
        *sd_missed = 0;
        *last_req_ts = 0;
        *last_rsp_ts = 0;
    }
}
