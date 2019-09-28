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

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_event_base.h>
#include <esp_wifi.h>
#include <wifi.h>
#include <cJSON.h>
#include <sys/param.h>
#include <esp_vfs.h>
#include <esp_spiffs.h>
#include <mdns.h>
#include <config.h>
#include <log.h>
#include "web_server.h"

/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)

#define WWW_PARTITION_PATH "/www"
#define BUFFER_SIZE 8192

static const char *TAG = "WEB";

static char *buffer;

#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

static esp_err_t www_spiffs_init() {
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
            .base_path = WWW_PARTITION_PATH,
            .partition_label = NULL,
            .max_files = 5,   // This decides the maximum number of files that can be created on the storage
            .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    return ESP_OK;
}

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    } else if (IS_FILE_EXT(filename, ".js")) {
        return httpd_resp_set_type(req, "application/javascript");
    } else if (IS_FILE_EXT(filename, ".css")) {
        return httpd_resp_set_type(req, "text/css");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
static char* get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash) {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

static esp_err_t log_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain");

    size_t length;
    void *log_data = log_receive(&length, 1);
    if (log_data == NULL) {
        httpd_resp_sendstr(req, "");

        return ESP_OK;
    }

    httpd_resp_send(req, log_data, length);

    log_return(log_data);

    return ESP_OK;
}

static esp_err_t file_get_handler(httpd_req_t *req) {
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    char *filename = get_path_from_uri(filepath, WWW_PARTITION_PATH,
                                             req->uri, sizeof(filepath));
    if (!filename) {
        ESP_LOGE(TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* If name has trailing '/', respond with directory contents */
    if (filename[strlen(filename) - 1] == '/' && strlen(filename) + strlen("index.html") < FILE_PATH_MAX) {
        strcpy(&filename[strlen(filename)], "index.html");
    }

    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

    if (!IS_FILE_EXT(filename, ".html")) {
        httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    }

    /* Retrieve the pointer to scratch buffer for temporary storage */
    //char *buffer = malloc(file_stat.st_size);
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(buffer, 1, BUFFER_SIZE, fd);

        /* Send the buffer contents as HTTP response chunk */
        if (httpd_resp_send_chunk(req, buffer, chunksize) != ESP_OK) {
            fclose(fd);
            ESP_LOGE(TAG, "File sending failed!");
            /* Abort sending file */
            httpd_resp_sendstr_chunk(req, NULL);
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
            return ESP_FAIL;
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t config_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();

    int config_item_count;
    const config_item_t *config_items = config_items_get(&config_item_count);
    for (int i = 0; i < config_item_count; i++) {
        const config_item_t *item = &config_items[i];

        int64_t int64 = 0;
        uint64_t uint64 = 0;
        size_t length = 0;
        char *string;

        if (item->secret) {
            string = calloc(1, 1);
        } else {
            switch (item->type) {
                case CONFIG_ITEM_TYPE_STRING:
                case CONFIG_ITEM_TYPE_BLOB:
                    ESP_ERROR_CHECK_WITHOUT_ABORT(config_get_str_blob(item, NULL, &length));
                    string = calloc(1, length + 1);
                    ESP_ERROR_CHECK_WITHOUT_ABORT(config_get_str_blob(item, string, &length));
                    string[length] = '\0';
                    break;
                case CONFIG_ITEM_TYPE_UINT8:
                case CONFIG_ITEM_TYPE_UINT16:
                case CONFIG_ITEM_TYPE_UINT32:
                case CONFIG_ITEM_TYPE_UINT64:
                    ESP_ERROR_CHECK_WITHOUT_ABORT(config_get_primitive(item, &uint64));
                    asprintf(&string, "%llu", uint64);
                    break;
                case CONFIG_ITEM_TYPE_BOOL:
                case CONFIG_ITEM_TYPE_INT8:
                case CONFIG_ITEM_TYPE_INT16:
                case CONFIG_ITEM_TYPE_INT32:
                case CONFIG_ITEM_TYPE_INT64:
                    ESP_ERROR_CHECK_WITHOUT_ABORT(config_get_primitive(item, &int64));
                    asprintf(&string, "%lld", int64);
                    break;
                default:
                    string = calloc(1, 1);
                    break;
            }
        }

        cJSON_AddStringToObject(root, item->key, string);

        free(string);
    }

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

static esp_err_t config_post_handler(httpd_req_t *req) {
    int ret = httpd_req_recv(req, buffer, BUFFER_SIZE - 1);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }

        return ESP_FAIL;
    }

    buffer[ret] = '\0';

    cJSON *root = cJSON_Parse(buffer);

    int config_item_count;
    const config_item_t *config_items = config_items_get(&config_item_count);
    for (int i = 0; i < config_item_count; i++) {
        char *key = config_items[i].key;
        config_item_type_t type = config_items[i].type;

        if (cJSON_HasObjectItem(root, key)) {
            cJSON *entry = cJSON_GetObjectItem(root, key);

            // Ignore empty values
            if (strlen(entry->valuestring) == 0) {
                continue;
            }

            esp_err_t err;
            if (type > CONFIG_ITEM_TYPE_MAX) {
                err = ESP_ERR_INVALID_ARG;
            } else if (type == CONFIG_ITEM_TYPE_STRING) {
                err = config_set_str(key, entry->valuestring);
            } else if (type == CONFIG_ITEM_TYPE_BLOB) {
                err = config_set_blob(key, entry->valuestring, strlen(entry->valuestring));
            } else if (type == CONFIG_ITEM_TYPE_BOOL) {
                err = config_set_boola(key,strcmp(entry->valuestring, "1") == 0);
            } else {
                bool is_zero = strcmp(entry->valuestring, "0") == 0 || strcmp(entry->valuestring, "0.0") == 0;
                int64_t int64 = strtol(entry->valuestring, NULL, 10);
                uint64_t uint64 = strtoul(entry->valuestring, NULL, 10);

                if (!is_zero && (int64 == 0 || uint64 == 0)) {
                    err = ESP_ERR_INVALID_ARG;
                } else {
                    switch (type) {
                        case CONFIG_ITEM_TYPE_INT8:
                            err = config_set_i8(key, int64);
                            break;
                        case CONFIG_ITEM_TYPE_INT16:
                            err = config_set_i16(key, int64);
                            break;
                        case CONFIG_ITEM_TYPE_INT32:
                            err = config_set_i32(key, int64);
                            break;
                        case CONFIG_ITEM_TYPE_INT64:
                            err = config_set_i64(key, int64);
                            break;
                        case CONFIG_ITEM_TYPE_UINT8:
                            err = config_set_u8(key, uint64);
                            break;
                        case CONFIG_ITEM_TYPE_UINT16:
                            err = config_set_u16(key, uint64);
                            break;
                        case CONFIG_ITEM_TYPE_UINT32:
                            err = config_set_u32(key, uint64);
                            break;
                        case CONFIG_ITEM_TYPE_UINT64:
                            err = config_set_u64(key, uint64);
                            break;
                        default:
                            err = ESP_FAIL;
                            break;
                    }
                }
            }

            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Set %s = %s (%d)", key, entry->valuestring, strlen(entry->valuestring));
            } else {
                ESP_LOGE(TAG, "Error setting %s = %s: %d - %s", key, entry->valuestring, err, esp_err_to_name(err));
            }
        }
    }

    config_commit();

    return ESP_OK;
}

static esp_err_t wifi_scan_get_handler(httpd_req_t *req) {
    uint16_t ap_count;
    wifi_ap_record_t *ap_records =  wifi_scan(&ap_count);

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < ap_count; i++) {
        wifi_ap_record_t *ap_record = &ap_records[i];
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddItemToArray(root, ap);
        cJSON_AddStringToObject(ap, "ssid", (char *) ap_record->ssid);
        cJSON_AddNumberToObject(ap, "rssi", ap_record->rssi);

        char *auth_mode = wifi_auth_mode_name(ap_record->authmode);
        cJSON_AddStringToObject(ap, "authmode", auth_mode);
    }

    free(ap_records);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

static httpd_handle_t web_server_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 2;
    config.uri_match_fn = httpd_uri_match_wildcard;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");

        httpd_uri_t uri_config_get = {
                .uri        = "/config",
                .method     = HTTP_GET,
                .handler    = config_get_handler
        };
        httpd_register_uri_handler(server, &uri_config_get);

        httpd_uri_t uri_config_post = {
                .uri        = "/config",
                .method     = HTTP_POST,
                .handler    = config_post_handler
        };
        httpd_register_uri_handler(server, &uri_config_post);

        httpd_uri_t uri_log_get = {
                .uri        = "/log",
                .method     = HTTP_GET,
                .handler    = log_get_handler
        };
        httpd_register_uri_handler(server, &uri_log_get);

        httpd_uri_t uri_wifi_scan_get = {
                .uri        = "/wifi_scan",
                .method     = HTTP_GET,
                .handler    = wifi_scan_get_handler
        };
        httpd_register_uri_handler(server, &uri_wifi_scan_get);

        httpd_uri_t uri_file_get = {
                .uri        = "/*",
                .method     = HTTP_GET,
                .handler    = file_get_handler
        };
        httpd_register_uri_handler(server, &uri_file_get);
    }

    if (server == NULL) {
        ESP_LOGE(TAG, "Could not start server");
        return NULL;
    }

    buffer = malloc(BUFFER_SIZE);

    return server;
}

static void web_server_stop(httpd_handle_t server)
{
    if (server) {
        free(buffer);

        // Stop the httpd server
        httpd_stop(server);
    }
}

void web_server_init() {
    www_spiffs_init();
    web_server_start();
}