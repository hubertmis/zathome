/*
 * Copyright (c) 2024 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "dfu_utils.h"

#include "continuous_sd.h"

#include <zephyr/kernel.h>
#include <zephyr/net/icmp.h>

static const struct sockaddr_in6 global_addr = {
    .sin6_family = AF_INET6,
    .sin6_addr = {
        .s6_addr = {0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x88},
    },
};

static volatile bool rsp_received;

static int icmp_handler(struct net_icmp_ctx *ctx,
			struct net_pkt *pkt,
			struct net_icmp_ip_hdr *hdr,
			struct net_icmp_hdr *icmp_hdr,
			void *user_data)
{
    (void)ctx;
    (void)pkt;
    (void)hdr;
    (void)icmp_hdr;
    (void)user_data;

	rsp_received = true;

	return 0;
}

bool dfu_utils_keep_checking_conectivity_until(int64_t uptime)
{
    unsigned int successes = 0;
    unsigned int failures = 0;
    int ret;

    do {
        struct net_icmp_ctx ctx;
        struct net_icmp_ping_params params = {
            .identifier = 0x0132,
            .sequence = successes + failures,
            .tc_tos = 1,
            .priority = 2,
            .data_size = 0,
        };

        rsp_received = false;
        ret  = net_icmp_init_ctx(&ctx, NET_ICMPV6_ECHO_REPLY, 0, icmp_handler);

        if (ret == 0) {
            ret = net_icmp_send_echo_request(&ctx, NULL,
    					 (struct sockaddr *)&global_addr,
    					 &params, NULL);
        }

        k_sleep(K_MSEC(10000));
        if (rsp_received) {
            successes++;
        } else {
            failures++;
        }
    } while (k_uptime_get() < uptime);

    return (successes > failures);
}
