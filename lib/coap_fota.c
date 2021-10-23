/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap_fota.h"

#include <coap_server.h>

#include <net/socket.h>
#include <net/coap.h>
#include <net/fota_download.h>

#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 64
#define MAX_FOTA_PAYLOAD_LEN 64
#define MAX_FOTA_PATH_LEN 16

int coap_fota_get(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[8];
    uint8_t *data;
    int r = 0;
    struct coap_packet response;
    char payload[] = CONFIG_MCUBOOT_IMAGE_VERSION;

    code = coap_header_get_code(request);
    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    if (type != COAP_TYPE_CON) {
        return -EINVAL;
    }

#if BLOCK_GLOBAL_ACCESS
    if (!addr_is_local(addr, addr_len) && !sock_is_secure(sock)) {
        // TODO: Send ACK Forbidden?
        return -EINVAL;
    }
#endif

    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
    if (!data) {
        return -ENOMEM;
    }

    r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
                 1, COAP_TYPE_ACK, tkl, token,
                 COAP_RESPONSE_CODE_CONTENT, id);
    if (r < 0) {
        goto end;
    }

    r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
            COAP_CONTENT_FORMAT_TEXT_PLAIN);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload_marker(&response);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload(&response, payload, sizeof(payload));
    if (r < 0) {
        goto end;
    }

    r = coap_server_send_coap_reply(sock, &response, addr, addr_len);

end:
    k_free(data);

    return r;
}

int coap_fota_post(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[COAP_TOKEN_MAX_LEN];
    const uint8_t *payload;
    uint16_t payload_len;
    char url[MAX_FOTA_PAYLOAD_LEN];
    char *path = NULL;
    static char fota_path[MAX_FOTA_PATH_LEN];

    code = coap_header_get_code(request);
    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    if (type != COAP_TYPE_CON) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    // Destination address or security is not verified here.
    // DTLS could be enforced to request FOTA. But it would create a risk of
    // unrecoverable bug if bug in DTLS prevents FOTA from starting. The risk
    // of requesting update with malicious firmware is minimized by signing
    // firmware images in FOTA procedure. Because of that it is acceptable to
    // allow FOTA request through unecrtypted CoAP connection regardless
    // destination address.

    payload = coap_packet_get_payload(request, &payload_len);
    if (!payload) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    if (payload_len >= sizeof(url)) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_REQUEST_TOO_LARGE, token, tkl);
        return -EINVAL;
    }

    // Start fota using sent URL
    memcpy(url, payload, payload_len);
    url[payload_len] = '\0';

    char *scheme_end = strstr(url, "://");
    if (scheme_end) {
        char *host = scheme_end + 3;

        char *host_end = strchr(host, '/');
        if (host_end) {
            *host_end = '\0';
            strncpy(fota_path, host_end + 1, sizeof(fota_path));
            path = fota_path;
        }
    }

    int fota_result = fota_download_start(url, path, -1, NULL, 0);

    if (fota_result) {
        coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_BAD_REQUEST, token, tkl);
        return -EINVAL;
    }

    coap_server_send_ack(sock, addr, addr_len, id, COAP_RESPONSE_CODE_CHANGED, token, tkl);
    return 0;
}
