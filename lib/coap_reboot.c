/*
 * Copyright (c) 2023 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap_reboot.h"

#include <coap_server.h>

#include <zcbor_decode.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/coap.h>
#include <zephyr/sys/reboot.h>

static int handle_reboot_post(zcbor_state_t *value,
	       	enum coap_response_code *rsp_code, void *context)
{
    (void)value;

    *rsp_code = COAP_RESPONSE_CODE_BAD_REQUEST;

    // TODO: Parse content? Delay or anything?

    sys_reboot(SYS_REBOOT_COLD);

    *rsp_code = COAP_RESPONSE_CODE_CHANGED;
    return 0;
}

int coap_reboot_post(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;

    return coap_server_handle_non_con_setter(sock, resource, request, addr, addr_len,
		    handle_reboot_post, NULL);
}
