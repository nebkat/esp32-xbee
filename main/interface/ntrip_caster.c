/*
 * This file is part of the ESP32-XBee distribution (https://github.com/nebkat/esp32-xbee).
 * Copyright (c) 2019 Nebojsa Cvetkovic.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <esp_log.h>
#include <esp_event_base.h>
#include <sys/socket.h>
#include <mdns.h>
#include <tasks.h>
#include <status_led.h>
#include <stream_stats.h>
#include <esp_ota_ops.h>
#include "interface/ntrip.h"
#include "config.h"
#include "util.h"
#include "uart.h"

static const char *TAG = "NTRIP_CASTER";

#define BUFFER_SIZE 512

static int sock = -1;

static status_led_handle_t status_led = NULL;
static stream_stats_handle_t stream_stats = NULL;

typedef struct ntrip_caster_client_t {
    int socket;
    SLIST_ENTRY(ntrip_caster_client_t) next;
} ntrip_caster_client_t;

static SLIST_HEAD(caster_clients_list_t, ntrip_caster_client_t) caster_clients_list;

static void ntrip_caster_client_remove(ntrip_caster_client_t *caster_client) {
    struct sockaddr_in6 client_addr;
    socklen_t socklen = sizeof(client_addr);
    int err = getpeername(caster_client->socket, (struct sockaddr *) &client_addr, &socklen);
    char *addr_str = err != 0 ? "UNKNOWN" : sockaddrtostr((struct sockaddr *) &client_addr);

    uart_nmea("$PESP,NTRIP,CST,CLIENT,DISCONNECTED,%s", addr_str);

    destroy_socket(&caster_client->socket);

    SLIST_REMOVE(&caster_clients_list, caster_client, ntrip_caster_client_t, next);
    free(caster_client);

    if (status_led != NULL && SLIST_EMPTY(&caster_clients_list)) status_led->flashing_mode = STATUS_LED_STATIC;
}

static void ntrip_caster_uart_handler(void* handler_args, esp_event_base_t base, int32_t length, void* buffer) {
    ntrip_caster_client_t *client, *client_tmp;
    SLIST_FOREACH_SAFE(client, &caster_clients_list, next, client_tmp) {
        int sent = write(client->socket, buffer, length);
        if (sent < 0) {
            ntrip_caster_client_remove(client);
        } else {
            stream_stats_increment(stream_stats, 0, sent);
        }
    }
}

static int ntrip_caster_socket_init() {
    int port = config_get_u16(CONF_ITEM(KEY_CONFIG_NTRIP_CASTER_PORT));

    sock = socket(PF_INET6, SOCK_STREAM, 0);
    ERROR_ACTION(TAG, sock < 0, return sock, "Could not create TCP socket: %d %s", errno, strerror(errno))

    int reuse = 1;
    int err = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ERROR_ACTION(TAG, err != 0, destroy_socket(&sock); return err, "Could not set TCP socket options: %d %s", errno, strerror(errno))

    struct sockaddr_in6 srv_addr = {
            .sin6_family = PF_INET6,
            .sin6_addr = IN6ADDR_ANY_INIT,
            .sin6_port = htons(port)
    };

    err = bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
    ERROR_ACTION(TAG, err != 0, destroy_socket(&sock); return err, "Could not bind TCP socket: %d %s", errno, strerror(errno))

    err = listen(sock, 1);
    ERROR_ACTION(TAG, err != 0, destroy_socket(&sock); return err, "Could not listen on TCP socket: %d %s", errno, strerror(errno))

    ESP_LOGI(TAG, "Listening on port %d", port);
    uart_nmea("$PESP,NTRIP,CST,BIND,%d", port);

    return 0;
}

static void ntrip_caster_task(void *ctx) {
    uart_register_read_handler(ntrip_caster_uart_handler);

    config_color_t status_led_color = config_get_color(CONF_ITEM(KEY_CONFIG_NTRIP_CASTER_COLOR));
    if (status_led_color.rgba != 0) status_led = status_led_add(status_led_color.rgba, STATUS_LED_STATIC, 500, 2000, 0);

    stream_stats = stream_stats_new("ntrip_caster");

    while (true) {
        ntrip_caster_socket_init();

        char *mountpoint, *username, *password;
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_NTRIP_CASTER_USERNAME), (void **) &username);
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_NTRIP_CASTER_PASSWORD), (void **) &password);
        config_get_str_blob_alloc(CONF_ITEM(KEY_CONFIG_NTRIP_CASTER_MOUNTPOINT), (void **) &mountpoint);

        // Wait for client connections
        int sock_client = -1;
        char *buffer = malloc(BUFFER_SIZE);
        while (true) {
            destroy_socket(&sock_client);

            struct sockaddr_in6 source_addr;
            size_t addr_len = sizeof(source_addr);
            sock_client = accept(sock, (struct sockaddr *)&source_addr, &addr_len);
            ERROR_ACTION(TAG, sock_client < 0, goto _error, "Could not accept connection: %d %s", errno, strerror(errno))

            int len = read(sock_client, buffer, BUFFER_SIZE - 1);
            ERROR_ACTION(TAG, len <= 0, continue, "Could not receive from client: %d %s", errno, strerror(errno))
            buffer[len] = '\0';

            // Find mountpoint requested by looking for GET /(%s)?
            char *mountpoint_path = extract_http_header(buffer, "GET ");
            ERROR_ACTION(TAG, mountpoint_path == NULL, {
                char *response = "HTTP/1.1 405 Method Not Allowed" NEWLINE \
                        "Allow: GET" NEWLINE \
                        NEWLINE;

                int err = write(sock_client, response, strlen(response));
                if (err < 0) ESP_LOGE(TAG, "Could not send response to client: %d %s", errno, strerror(errno));

                continue;
            }, "Client did not send GET request")

            // Move pointer to name of mountpoint, or empty string if sourcetable request
            char *mountpoint_name = mountpoint_path;

            // Treat GET /mountpoint and GET mountpoint the same
            if (mountpoint_name[0] == '/') mountpoint_name++;

            // Move to space or end of string (removing HTTP/1.1 from line)
            char *space = strstr(mountpoint_name, " ");
            if (space != NULL) *space = '\0';

            // Print sourcetable if exact mountpoint was not requested
            bool print_sourcetable = strcasecmp(mountpoint, mountpoint_name) != 0;
            free(mountpoint_path);

            // Ensure authenticated
            char *basic_authentication = strlen(username) == 0 ? NULL : http_auth_basic_header(username, password);
            char *authorization_header = extract_http_header(buffer, "Authorization:");
            bool authenticated = basic_authentication == NULL ||
                    (authorization_header != NULL && strcasecmp(basic_authentication, authorization_header) == 0);
            free(basic_authentication);
            free(authorization_header);

            // Use HTTP response if not an NTRIP client
            char *user_agent_header = extract_http_header(buffer, "User-Agent:");
            bool ntrip_agent = user_agent_header == NULL || strcasestr(user_agent_header, "NTRIP") != NULL;
            free(user_agent_header);

            // Unknown mountpoint or sourcetable requested
            if (print_sourcetable) {
                char stream[256] = "";
                snprintf(stream, sizeof(stream), "STR;%s;;;;;;;;0.00;0.00;0;0;;none;%c;N;0;" NEWLINE "ENDSOURCETABLE",
                        mountpoint, strlen(username) == 0 ? 'N' : 'B');

                snprintf(buffer, BUFFER_SIZE, "%s 200 OK" NEWLINE \
                        "Server: NTRIP %s/%s" NEWLINE \
                        "Content-Type: text/plain" NEWLINE \
                        "Content-Length: %d" NEWLINE \
                        "Connection: close" NEWLINE \
                        NEWLINE \
                        "%s",
                        ntrip_agent ? "SOURCETABLE" : "HTTP/1.0",
                        NTRIP_CASTER_NAME, &esp_ota_get_app_description()->version[1],
                        strlen(stream), stream);

                int err = write(sock_client, buffer, strlen(buffer));
                if (err < 0) ESP_LOGE(TAG, "Could not send response to client: %d %s", errno, strerror(errno));

                continue;
            }

            // Request basic authentication header
            if (!authenticated) {
                char *message = "Authorization Required";
                snprintf(buffer, BUFFER_SIZE, "HTTP/1.0 401 Unauthorized" NEWLINE \
                        "Server: %s/1.0" NEWLINE \
                        "WWW-Authenticate: Basic realm=\"/%s\"" NEWLINE
                        "Content-Type: text/plain" NEWLINE \
                        "Content-Length: %d" NEWLINE \
                        "Connection: close" NEWLINE \
                        NEWLINE \
                        "%s",
                        NTRIP_CASTER_NAME, mountpoint, strlen(message), message);

                int err = write(sock_client, buffer, strlen(buffer));
                if (err < 0) ESP_LOGE(TAG, "Could not send response to client: %d %s", errno, strerror(errno));

                continue;
            }

            char response[] = "ICY 200 OK" NEWLINE NEWLINE;
            int err = write(sock_client, response, sizeof(response));
            ERROR_ACTION(TAG, err < 0, continue, "Could not send response to client: %d %s", errno, strerror(errno))

            ntrip_caster_client_t *client = malloc(sizeof(ntrip_caster_client_t));
            client->socket = sock_client;
            SLIST_INSERT_HEAD(&caster_clients_list, client, next);

            // Socket will now be dealt with by ntrip_caster_uart_handler, set to -1 so it doesn't get destroyed
            sock_client = -1;

            if (status_led != NULL) status_led->flashing_mode = STATUS_LED_FADE;

            char *addr_str = sockaddrtostr((struct sockaddr *) &source_addr);
            uart_nmea("$PESP,NTRIP,CST,CLIENT,CONNECTED,%s", addr_str);
        }

        _error:
        destroy_socket(&sock);

        free(buffer);
    }
}

void ntrip_caster_init() {
    if (!config_get_bool1(CONF_ITEM(KEY_CONFIG_NTRIP_CASTER_ACTIVE))) return;

    xTaskCreate(ntrip_caster_task, "ntrip_caster_task", 4096, NULL, TASK_PRIORITY_INTERFACE, NULL);
}