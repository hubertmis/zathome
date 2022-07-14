/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap_sd.h"

#include <errno.h>
#include <stdint.h>

#include "coap_server.h"

#include <net/socket.h>
#include <net/coap.h>
#include <net/tls_credentials.h>
#include <random/rand32.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>
#include <zephyr.h>

#define COAP_PORT 5683
#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 64

#define SD_FLT_NAME "name"
#define SD_FLT_TYPE "type"
#define SD_RSRC "sd"
#define SD_NAME_MAX_LEN 8
#define SD_TYPE_MAX_LEN 8

// Server

#ifdef CONFIG_COAP_SD_MAX_NUM_RSRCS
#define NUM_RSRCS CONFIG_COAP_SD_MAX_NUM_RSRCS
#else
#define NUM_RSRCS 2
#endif

static struct {
    const char *name;
    const char *type;
} rsrcs[NUM_RSRCS];

static bool filter_sd_req(const uint8_t *payload, uint16_t payload_len)
{
    CborError cbor_error;
    CborParser parser;
    CborValue value;
    struct cbor_buf_reader reader;
    const char *expected_type = NULL;

    cbor_buf_reader_init(&reader, payload, payload_len);

    cbor_error = cbor_parser_init(&reader.r, 0, &parser, &value);
    if (cbor_error != CborNoError) {
        return false;
    }

    if (!cbor_value_is_map(&value)) {
        return false;
    }

    CborValue map_val;

    // Handle name
    cbor_error = cbor_value_map_find_value(&value, SD_FLT_NAME, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_text_string(&map_val)) {
        size_t str_len;

        cbor_error = cbor_value_get_string_length(&map_val, &str_len);
        if ((cbor_error == CborNoError) && (str_len < SD_NAME_MAX_LEN)) {
            char str[SD_NAME_MAX_LEN];
            str_len = SD_NAME_MAX_LEN;

            cbor_error = cbor_value_copy_text_string(&map_val, str, &str_len, NULL);
            if (cbor_error == CborNoError) {
                bool found = false;

                for (int i = 0; i < ARRAY_SIZE(rsrcs); ++i) {
                    if (strncmp(str, rsrcs[i].name, SD_NAME_MAX_LEN) == 0) {
                        found = true;
                        expected_type = rsrcs[i].type;
                        break;
                    }
                }

                if (!found) {
                    return false;
                }
            }
        }
    }

    // Handle type
    cbor_error = cbor_value_map_find_value(&value, SD_FLT_TYPE, &map_val);
    if ((cbor_error == CborNoError) && cbor_value_is_text_string(&map_val)) {
        size_t str_len;

        cbor_error = cbor_value_get_string_length(&map_val, &str_len);
        if ((cbor_error == CborNoError) && (str_len < SD_TYPE_MAX_LEN)) {
            char str[SD_TYPE_MAX_LEN];
            str_len = SD_TYPE_MAX_LEN;

            cbor_error = cbor_value_copy_text_string(&map_val, str, &str_len, NULL);
            if (cbor_error == CborNoError) {
                bool found = false;

                if (expected_type) {
                    if (strncmp(str, expected_type, SD_TYPE_MAX_LEN) != 0) {
                        return false;
                    }
                }

                for (int i = 0; i < ARRAY_SIZE(rsrcs); ++i) {
                    if (strncmp(str, rsrcs[i].type, SD_TYPE_MAX_LEN) == 0) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    return false;
                }
            }
        }
    }

    return true;
}

static int prepare_sd_rsp_payload(uint8_t *payload, size_t len)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;
    int num_rsrcs = 0;

    // TODO: Mutex while accessing resources array?
    for (int i = 0; i < ARRAY_SIZE(rsrcs); ++i) {
        if (rsrcs[i].name && rsrcs[i].type) {
            num_rsrcs++;
        }
    }

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, num_rsrcs) != CborNoError) return -EINVAL;

    for (int i = 0; i < ARRAY_SIZE(rsrcs); ++i) {
        if (rsrcs[i].name && rsrcs[i].type) {
            CborEncoder rsrc_map;
            if (cbor_encode_text_string(&map, rsrcs[i].name, strlen(rsrcs[i].name)) != CborNoError) return -EINVAL;
            if (cbor_encoder_create_map(&map, &rsrc_map, 1) != CborNoError) return -EINVAL;

            if (cbor_encode_text_string(&rsrc_map, SD_FLT_TYPE, strlen(SD_FLT_TYPE)) != CborNoError) return -EINVAL;
            if (cbor_encode_text_string(&rsrc_map, rsrcs[i].type, strlen(rsrcs[i].type)) != CborNoError) return -EINVAL;

            if (cbor_encoder_close_container(&map, &rsrc_map) != CborNoError) return -EINVAL;
        }
    }

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int send_sd_rsp(int sock,
                       const struct sockaddr *addr, socklen_t addr_len,
                       uint8_t *token, uint8_t tkl)
{
    uint8_t *data;
    int r = 0;
    struct coap_packet response;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];

    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
    if (!data) {
        return -ENOMEM;
    }

    r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
                 1, COAP_TYPE_NON_CON, tkl, token,
                 COAP_RESPONSE_CODE_CONTENT, coap_next_id());
    if (r < 0) {
        goto end;
    }

    r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
            COAP_CONTENT_FORMAT_APP_CBOR);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload_marker(&response);
    if (r < 0) {
        goto end;
    }

    r = prepare_sd_rsp_payload(payload, MAX_COAP_PAYLOAD_LEN);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload(&response, payload, r);
    if (r < 0) {
        goto end;
    }

    r = coap_server_send_coap_reply(sock, &response, addr, addr_len);

end:
    k_free(data);

    return r;
}

int coap_sd_server(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[8];
    int r = 0;
    struct coap_option option;
    const uint8_t *payload;
    uint16_t payload_len;

    bool opt_cf_present = false;
    bool opt_cf_correct = false;
    bool payload_present = false;
    bool filter_passed = true;

    code = coap_header_get_code(request);
    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    if (type != COAP_TYPE_NON_CON) {
        return -EINVAL;
    }

#if BLOCK_GLOBAL_ACCESS
    if (!addr_is_local(addr, addr_len) && !sock_is_secure(sock)) {
        return -EINVAL;
    }
#endif

    r = coap_find_options(request, COAP_OPTION_CONTENT_FORMAT, &option, 1); 
    if (r == 1) {
        opt_cf_present = true;
    }

    if (opt_cf_present && (coap_option_value_to_int(&option) == COAP_CONTENT_FORMAT_APP_CBOR)) {
        opt_cf_correct = true;
    }

    payload = coap_packet_get_payload(request, &payload_len);
    if (payload) {
        payload_present = true;
    }

    if (opt_cf_present && (!opt_cf_correct || !payload_present)) {
        return -EINVAL;
    }

    if (opt_cf_present && opt_cf_correct && payload_present) {
        filter_passed = filter_sd_req(payload, payload_len);
    }

    if (filter_passed) {
        k_sleep(K_MSEC(sys_rand32_get() % 512));
        r = send_sd_rsp(sock, addr, addr_len, token, tkl);
    } else {
        r = 0;
    }

    return r;
}

int coap_sd_server_register_rsrc(const char *name, const char *type)
{
    // TODO: Mutex?

    for (size_t i = 0; i < ARRAY_SIZE(rsrcs); ++i) {
        if ((rsrcs[i].name == NULL) && (rsrcs[i].type == NULL)) {
            rsrcs[i].name = name;
            rsrcs[i].type = type;
            return 0;
        }
    }

    return -1;
}

void coap_sd_server_clear_all_rsrcs(void)
{
    // TODO: Mutex?

    for (size_t i = 0; i < ARRAY_SIZE(rsrcs); ++i) {
        rsrcs[i].name = NULL;
        rsrcs[i].type = NULL;
    }
}

// Client
static int prepare_sd_req_payload(uint8_t *payload, size_t len, const char *name, const char *type)
{
    struct cbor_buf_writer writer;
    CborEncoder ce;
    CborEncoder map;

    bool name_known = (name != NULL) && (strlen(name) > 0);
    bool type_known = (type != NULL) && (strlen(type) > 0);

    int num_filters = 0;

    if (name_known) num_filters++;
    if (type_known) num_filters++;

    if (!num_filters) {
        // There are no filters to add as payload
        return 0;
    }

    cbor_buf_writer_init(&writer, payload, len);
    cbor_encoder_init(&ce, &writer.enc, 0);

    if (cbor_encoder_create_map(&ce, &map, num_filters) != CborNoError) return -EINVAL;

    if (name_known) {
        if (cbor_encode_text_string(&map, SD_FLT_NAME, strlen(SD_FLT_NAME)) != CborNoError) return -EINVAL;
        if (cbor_encode_text_string(&map, name, strlen(name)) != CborNoError) return -EINVAL;
    }

    if (type_known) {
        if (cbor_encode_text_string(&map, SD_FLT_TYPE, strlen(SD_FLT_TYPE)) != CborNoError) return -EINVAL;
        if (cbor_encode_text_string(&map, type, strlen(type)) != CborNoError) return -EINVAL;
    }

    if (cbor_encoder_close_container(&ce, &map) != CborNoError) return -EINVAL;

    return (size_t)(writer.ptr - payload);
}

static int coap_sd_send_req(const char *name, const char *type, int sock, bool mesh)
{
    int r;
    struct coap_packet cpkt;
    uint8_t *data;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    struct sockaddr_in6 addr = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(COAP_PORT),
        .sin6_addr = {
            .s6_addr = {0xff, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}
        },
    };

    if (mesh) {
        addr.sin6_addr.s6_addr[1] = 0x03;
    }

    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
    if (!data) {
        return -ENOMEM;
    }

    r = coap_packet_init(&cpkt, data, MAX_COAP_MSG_LEN,
                 1, COAP_TYPE_NON_CON, 4, coap_next_token(),
                 COAP_METHOD_GET, coap_next_id());
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_option(&cpkt, COAP_OPTION_URI_PATH, SD_RSRC, strlen(SD_RSRC));
    if (r < 0) {
        goto end;
    }

    r = coap_append_option_int(&cpkt, COAP_OPTION_CONTENT_FORMAT,
            COAP_CONTENT_FORMAT_APP_CBOR);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload_marker(&cpkt);
    if (r < 0) {
        goto end;
    }

    r = prepare_sd_req_payload(payload, MAX_COAP_PAYLOAD_LEN, name, type);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload(&cpkt, payload, r);
    if (r < 0) {
        goto end;
    }

    r = sendto(sock, cpkt.data, cpkt.offset, 0, (struct sockaddr *)&addr, sizeof(addr));
    if (r < 0) {
        r = -errno;
    }

end:
    k_free(data);

    return r;
}

static int coap_sd_process_rsp(int sock, 
                               uint8_t *data, size_t data_len,
                               const coap_sd_found cb,
                               const struct sockaddr *addr,
                               const socklen_t *addr_len,
                               const char *name,
                               const char *type)
{
    struct coap_packet rsp;
    int r;
    uint8_t coap_type;
    struct coap_option option;
    const uint8_t *payload;
    uint16_t payload_len;

    r = coap_packet_parse(&rsp, data, data_len, NULL, 0);
    if (r < 0) {
        return r;
    }

    coap_type = coap_header_get_type(&rsp);

    if (coap_type != COAP_TYPE_NON_CON) {
        return -EINVAL;
    }

#if BLOCK_GLOBAL_ACCESS
    if (!addr_is_local(addr, *addr_len) && !sock_is_secure(sock)) {
        return -EINVAL;
    }
#endif

    r = coap_find_options(&rsp, COAP_OPTION_CONTENT_FORMAT, &option, 1); 
    if (r != 1) {
        return -EINVAL;
    }

    if (coap_option_value_to_int(&option) != COAP_CONTENT_FORMAT_APP_CBOR) {
        return -EINVAL;
    }

    payload = coap_packet_get_payload(&rsp, &payload_len);
    if (!payload) {
        return -EINVAL;
    }

    CborError cbor_error;
    CborParser parser;
    CborValue value;
    struct cbor_buf_reader reader;
    char rcvd_name[SD_NAME_MAX_LEN];
    char rcvd_type[SD_TYPE_MAX_LEN];

    cbor_buf_reader_init(&reader, payload, payload_len);

    cbor_error = cbor_parser_init(&reader.r, 0, &parser, &value);
    if (cbor_error != CborNoError) {
        return -EINVAL;
    }

    if (!cbor_value_is_map(&value)) {
        return -EINVAL;
    }

    size_t top_map_len;
    cbor_error = cbor_value_get_map_length(&value, &top_map_len);
    if (cbor_error != CborNoError) {
        return -EINVAL;
    }

    CborValue map_val;
    cbor_error = cbor_value_enter_container(&value, &map_val);
    if (cbor_error != CborNoError) {
        return -EINVAL;
    }

    for (size_t i = 0; i < top_map_len; ++i) {
        if (!cbor_value_is_text_string(&map_val)) {
            // Skip key and value
            for (size_t j = 0; j < 2; ++j) {
                cbor_error = cbor_value_advance(&map_val);
                if (cbor_error != CborNoError) {
                    return -EINVAL;
                }
            }
            // And check next key
            continue;
        }

        // Get key: name
        size_t str_len;

        cbor_error = cbor_value_get_string_length(&map_val, &str_len);
        if ((cbor_error != CborNoError) || (str_len >= SD_NAME_MAX_LEN)) {
            // Skip key and value
            for (size_t j = 0; j < 2; ++j) {
                cbor_error = cbor_value_advance(&map_val);
                if (cbor_error != CborNoError) {
                    return -EINVAL;
                }
            }
            // And check next key
            continue;
        }

        str_len = SD_NAME_MAX_LEN;

        cbor_error = cbor_value_copy_text_string(&map_val, rcvd_name, &str_len, NULL);
        if (cbor_error != CborNoError) {
            // Skip key and value
            for (size_t j = 0; j < 2; ++j) {
                cbor_error = cbor_value_advance(&map_val);
                if (cbor_error != CborNoError) {
                    return -EINVAL;
                }
            }
            // And check next key
            continue;
        }

        if (name && strlen(name)) {
            if (strncmp(name, rcvd_name, SD_NAME_MAX_LEN) != 0) {
                // Skip key and value
                for (size_t j = 0; j < 2; ++j) {
                    cbor_error = cbor_value_advance(&map_val);
                    if (cbor_error != CborNoError) {
                        return -EINVAL;
                    }
                }
                // And check next key
                continue;
            }
        }

        // Name is OK. Now get type

        // Skip key
        cbor_error = cbor_value_advance(&map_val);
        if (cbor_error != CborNoError) {
            return -EINVAL;
        }

        if (!cbor_value_is_map(&map_val)) {
            // Skip value
            cbor_error = cbor_value_advance(&map_val);
            if (cbor_error != CborNoError) {
                return -EINVAL;
            }
            // And check next key
            continue;
        }

        CborValue type_val;
        cbor_error = cbor_value_map_find_value(&map_val, SD_FLT_TYPE, &type_val);
        if ((cbor_error != CborNoError) || !cbor_value_is_text_string(&type_val)) {
            // Skip value
            cbor_error = cbor_value_advance(&map_val);
            if (cbor_error != CborNoError) {
                return -EINVAL;
            }
            // And check next key
            continue;
        }

        cbor_error = cbor_value_get_string_length(&type_val, &str_len);
        if ((cbor_error != CborNoError) || (str_len >= SD_TYPE_MAX_LEN)) {
            // Skip value
            cbor_error = cbor_value_advance(&map_val);
            if (cbor_error != CborNoError) {
                return -EINVAL;
            }
            // And check next key
            continue;
        }

        str_len = SD_TYPE_MAX_LEN;

        cbor_error = cbor_value_copy_text_string(&type_val, rcvd_type, &str_len, NULL);
        if (cbor_error != CborNoError) {
            // Skip value
            cbor_error = cbor_value_advance(&map_val);
            if (cbor_error != CborNoError) {
                return -EINVAL;
            }
            // And check next key
            continue;
        }

        if (type && strlen(type)) {
            if (strncmp(type, rcvd_type, SD_TYPE_MAX_LEN) != 0) {
                // Skip value
                cbor_error = cbor_value_advance(&map_val);
                if (cbor_error != CborNoError) {
                    return -EINVAL;
                }
                // And check next key
                continue;
            }
        }

        // Type is also accepted. Notify higher layer
        if (cb) {
            cb(addr, addr_len, rcvd_name, rcvd_type);
        }

        // Skip value. Continue with next key
        cbor_error = cbor_value_advance(&map_val);
        if (cbor_error != CborNoError) {
            return -EINVAL;
        }
    }

    return 0;
}

static int coap_sd_receive_rsp(int sock,
                               const coap_sd_found cb,
                               const char *name,
                               const char *type)
{
    int r;
    struct sockaddr addr;
    socklen_t addr_len;
    uint8_t response[MAX_COAP_MSG_LEN];

    while (1) {
        addr_len = sizeof(addr);
        r = recvfrom(sock, response, sizeof(response), 0, &addr, &addr_len);
        if (r < 0) {
            if (errno == EAGAIN) {
                // Expecting timeout
                return 0;
            } else {
                return -errno;
            }
        }

        coap_sd_process_rsp(sock, response, r, cb, &addr, &addr_len, name, type);
    }
}


int coap_sd_start(const char *name, const char *type, coap_sd_found cb, bool mesh)
{
    // Prepare socket
    int r;
    int sock;
    struct timeval timeout = {
        .tv_sec = 4,
    };

    sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return -errno;
    }

    r = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (r < 0) {
        r = -errno;
        goto end;
    }

    // Send request
    r = coap_sd_send_req(name, type, sock, mesh);
    if (r < 0) {
        goto end;
    }

    // Get responses and execute callback for each valid one
    r = coap_sd_receive_rsp(sock, cb, name, type);
end:
    close(sock);

    return r;
}
