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

#include <web_server.h>
#include <log.h>
#include <status_led.h>
#include <socket_client.h>
#include <esp_sntp.h>
#include <core_dump.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/ledc.h"
#include "button.h"

#include "config.h"
#include "wifi.h"
#include "socket_server.h"
#include "uart.h"
#include "ntrip.h"
#include "tasks.h"

static const char *TAG = "MAIN";

static char *reset_reason_name(esp_reset_reason_t reason);

static void reset_button_task() {
    QueueHandle_t button_queue = button_init(PIN_BIT(GPIO_NUM_0));
    gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);
    while (true) {
        button_event_t button_ev;
        if (xQueueReceive(button_queue, &button_ev, 1000 / portTICK_PERIOD_MS)) {
            if (button_ev.event == BUTTON_DOWN && button_ev.duration > 5000) {
                config_reset();
                vTaskDelay(2000 / portTICK_PERIOD_MS);
                esp_restart();
            }
        }
    }
}

static void sntp_time_set_handler(struct timeval *tv) {
    ESP_LOGI(TAG, "Synced time from SNTP");
}

void app_main()
{
    status_led_init();
    status_led_add(0xFFFFFF33, STATUS_LED_BLINK, 100, 1000, 0);

    log_init();
    esp_log_set_vprintf(log_vprintf);
    esp_log_level_set("gpio", ESP_LOG_WARN);
    esp_log_level_set("system_api", ESP_LOG_WARN);
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("tcpip_adapter", ESP_LOG_WARN);

    core_dump_check();

    xTaskCreate(reset_button_task, "reset_button", 4096, NULL, TASK_PRIORITY_RESET_BUTTON, NULL);

    config_init();
    uart_init();

    esp_reset_reason_t reset_reason = esp_reset_reason();

    uart_nmea("$PESP,INIT,START,%s,%s", PROJECT_VER, reset_reason_name(reset_reason));

    ESP_LOGI(TAG, "╔══════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║ ESP32 XBee UART Interface                    ║");
    ESP_LOGI(TAG, "╠══════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║ Version: %-35s "                            "║", PROJECT_VER);
    ESP_LOGI(TAG, "║ Reset reason: %-30s "                       "║", reset_reason_name(reset_reason));
    ESP_LOGI(TAG, "╟──────────────────────────────────────────────╢");
    ESP_LOGI(TAG, "║ Author: Nebojša Cvetković                    ║");
    ESP_LOGI(TAG, "║ Source: https://github.com/nebkat/esp32-xbee ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════╝");

    esp_event_loop_create_default();

    tcpip_adapter_init();

    wifi_init();

    web_server_init();

    ntrip_caster_init();
    ntrip_server_init();
    ntrip_client_init();

    socket_server_init();
    socket_client_init();

    uart_nmea("$PESP,INIT,COMPLETE");

    wait_for_ip();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    sntp_set_time_sync_notification_cb(sntp_time_set_handler);
    sntp_init();
}

static char *reset_reason_name(esp_reset_reason_t reason) {
    switch (reason) {
        default:
        case ESP_RST_UNKNOWN:
            return "UNKNOWN";
        case ESP_RST_POWERON:
            return "POWERON";
        case ESP_RST_EXT:
            return "EXTERNAL";
        case ESP_RST_SW:
            return "SOFTWARE";
        case ESP_RST_PANIC:
            return "PANIC";
        case ESP_RST_INT_WDT:
            return "INTERRUPT_WATCHDOG";
        case ESP_RST_TASK_WDT:
            return "TASK_WATCHDOG";
        case ESP_RST_WDT:
            return "OTHER_WATCHDOG";
        case ESP_RST_DEEPSLEEP:
            return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:
            return "BROWNOUT";
        case ESP_RST_SDIO:
            return "SDIO";
    }
}