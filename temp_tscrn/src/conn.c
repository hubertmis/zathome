/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "conn.h"

#include <assert.h>

#include <net/fota_download.h>
#include <net/openthread.h>

#include <openthread/coap.h>

#define FOTA_PAYLOAD_MAX_SIZE 64
#define FOTA_PATH_MAX_SIZE 16

static void ack_send(otMessage *request, const otMessageInfo *message_info, bool secure, otCoapCode code)
{
    otError    error = OT_ERROR_NONE;
    otMessage *response;

    response = otCoapNewMessage(openthread_get_default_instance(), NULL);
    if (response == NULL)
    {
        goto exit;
    }

    otCoapMessageInitResponse(response, request, OT_COAP_TYPE_ACKNOWLEDGMENT, code);

    if (secure)
    {
        //error = otCoapSecureSendResponse(openthread_get_default_instance(), response, message_info);
    }
    else
    {
        error = otCoapSendResponse(openthread_get_default_instance(), response, message_info);
    }

exit:
    if (error != OT_ERROR_NONE && response != NULL)
    {
        otMessageFree(response);
    }
}

static char fota_path[FOTA_PATH_MAX_SIZE];

static void fota_put_handler(otMessage *message, const otMessageInfo *message_info, bool secure)
{
    otCoapCode code = OT_COAP_CODE_BAD_REQUEST;

    char payload[FOTA_PAYLOAD_MAX_SIZE];
    int payload_len;
    char *url = NULL;
    char *path = NULL;

    payload_len = otMessageRead(message, otMessageGetOffset(message), payload, sizeof(payload));

    if (payload_len < sizeof(payload)) {
        url = payload;
        url[payload_len] = '\0';

        char *scheme_end = strstr(url, "://");
        if (scheme_end) {
            char *host = scheme_end + 3;

            char *host_end = strchr(host, '/');
            if (host_end) {
                *host_end = '\0';
                strncpy(fota_path, host_end + 1, FOTA_PATH_MAX_SIZE);
                path = fota_path;
            }
        }

        int fota_result = fota_download_start(url, path, -1, NULL, 0);

        if (!fota_result) {
            code = OT_COAP_CODE_CHANGED;
        }
    }

    ack_send(message, message_info, secure, code);
}

static void fota_details_send(otMessage *request, const otMessageInfo *message_info, bool secure)
{
    otError    error = OT_ERROR_NONE;
    otMessage *response;

    const char payload[] = CONFIG_MCUBOOT_IMAGE_VERSION;
    size_t payload_size;

    response = otCoapNewMessage(openthread_get_default_instance(), NULL);
    if (response == NULL)
    {
        goto exit;
    }

    otCoapMessageInitResponse(response, request, OT_COAP_TYPE_ACKNOWLEDGMENT, OT_COAP_CODE_CONTENT);
    otCoapMessageAppendContentFormatOption(response, OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN);
    (void)otCoapMessageSetPayloadMarker(response);

    payload_size = strlen(payload);

    error = otMessageAppend(response, payload, payload_size);
    if (error != OT_ERROR_NONE)
    {
        goto exit;
    }

    if (secure)
    {
        //error = otCoapSecureSendResponse(openthread_get_default_instance(), response, message_info);
    }
    else
    {
        error = otCoapSendResponse(openthread_get_default_instance(), response, message_info);
    }

exit:
    if (error != OT_ERROR_NONE && response != NULL)
    {
        otMessageFree(response);
    }
}

static void fota_handler(void *context, otMessage *message, const otMessageInfo *message_info)
{
    // TODO: At least verify if destination address is site local
    bool secure = false;

    if (otCoapMessageGetType(message) != OT_COAP_TYPE_CONFIRMABLE)
    {
        return;
    }

    switch (otCoapMessageGetCode(message))
    {
        case OT_COAP_CODE_GET:
            // TODO: Move it to another resource like version. Keep data format between GET and PUT identical. PUT takes URL, if get is implemented it should show last used URL
            fota_details_send(message, message_info, secure);
            break;

        case OT_COAP_CODE_PUT:
            fota_put_handler(message, message_info, secure);
            break;

        default:
            ack_send(message, message_info, secure, OT_COAP_CODE_METHOD_NOT_ALLOWED);
            break;
    }
}

static otCoapResource test_resource = { 
        .mUriPath = "fota_req", 
        .mHandler = fota_handler, 
        .mContext = NULL, 
};

void conn_init(void)
{
    otError error;
    error = otCoapStart(openthread_get_default_instance(), OT_DEFAULT_COAP_PORT);
    assert(!error);

    otCoapAddResource(openthread_get_default_instance(), &test_resource);
}
