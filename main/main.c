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

#include <string.h>
#include <web_server.h>
#include <esp_app_trace.h>
#include <mdns.h>
#include <log.h>
#include <status_led.h>
#include <nmea.h>
#include <lwip/inet.h>
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

void app_main()
{
    status_led_init();
    status_led_handle_t setup_led_white = status_led_add(0xFFFFFF33, STATUS_LED_BLINK, 100, 2500, 0);

    xTaskCreate(reset_button_task, "reset_button", 4096, NULL, TASK_PRIORITY_RESET_BUTTON, NULL);

    log_init();
    esp_log_set_vprintf(log_vprintf);
    esp_log_level_set("gpio", ESP_LOG_WARN);
    esp_log_level_set("system_api", ESP_LOG_WARN);
    esp_log_level_set("wifi", ESP_LOG_WARN);

    ESP_LOGI(TAG, "Starting ESP32 XBee UART Interface");
    ESP_LOGI(TAG, "Author: Nebojša Cvetković");
    ESP_LOGI(TAG, "Source: https://github.com/nebkat/esp32-ntrip-server");

    esp_event_loop_create_default();

    config_init();
    uart_init();
    tcpip_adapter_init();

    wifi_init();
    //bluetooth_init();

    web_server_init();

    ntrip_caster_init();
    ntrip_server_init();

    vTaskDelay(10000 / portTICK_PERIOD_MS);

    ntrip_client_init();

    socket_server_init();
}