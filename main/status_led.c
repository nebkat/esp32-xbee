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

#include <driver/ledc.h>
#include <tasks.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/xtensa_api.h"
#include "freertos/portmacro.h"
#include "status_led.h"
#include <sys/queue.h>

#define LEDC_SPEED_MODE LEDC_HIGH_SPEED_MODE

#define STATUS_LED_RED_GPIO GPIO_NUM_21
#define STATUS_LED_GREEN_GPIO GPIO_NUM_22
#define STATUS_LED_BLUE_GPIO GPIO_NUM_23
#define STATUS_LED_RED_CHANNEL LEDC_CHANNEL_0
#define STATUS_LED_GREEN_CHANNEL LEDC_CHANNEL_1
#define STATUS_LED_BLUE_CHANNEL LEDC_CHANNEL_2

#define STATUS_LED_RSSI_GPIO GPIO_NUM_18
#define STATUS_LED_SLEEP_GPIO GPIO_NUM_27
#define STATUS_LED_ASSOC_GPIO GPIO_NUM_25
#define STATUS_LED_RSSI_CHANNEL LEDC_CHANNEL_3
#define STATUS_LED_SLEEP_CHANNEL LEDC_CHANNEL_4
#define STATUS_LED_ASSOC_CHANNEL LEDC_CHANNEL_5

#define STATUS_LED_FREQ 1000

static SLIST_HEAD(status_led_color_list_t, status_led_color_t) status_led_colors_list;

static TaskHandle_t led_task;

void status_led_clear() {

}

status_led_handle_t status_led_add(uint32_t rgba, status_led_flashing_mode_t flashing_mode, uint32_t interval, uint32_t duration, uint8_t expire) {
    uint8_t red = (rgba >> 24u) & 0xFFu;
    uint8_t green = (rgba >> 16u) & 0xFFu;
    uint8_t blue = (rgba >> 8u) & 0xFFu;
    uint8_t alpha = rgba & 0xFFu;

    status_led_handle_t color = calloc(1, sizeof(struct status_led_color_t));
    color->red = (red * alpha) / 0xFF;
    color->green = (green * alpha) / 0xFF;
    color->blue = (blue * alpha) / 0xFF;

    color->flashing_mode = flashing_mode;
    color->interval = interval;
    color->duration = duration;
    color->expire = expire;

    color->active = true;

    // Insert at tail
    if (SLIST_EMPTY(&status_led_colors_list)) {
        SLIST_INSERT_HEAD(&status_led_colors_list, color, next);
    } else {
        status_led_handle_t current, next;
        SLIST_FOREACH_SAFE(current, &status_led_colors_list, next, next) {
            if (next == NULL) {
                SLIST_INSERT_AFTER(current, color, next);
            }
        }
    }

    vTaskResume(led_task);

    return color;
}

void status_led_remove(status_led_handle_t color) {
    if (color == NULL) return;
    color->remove = true;
}

static void status_led_channel_set(ledc_channel_t channel, uint8_t value) {
    ledc_set_duty(LEDC_SPEED_MODE, channel, value);
    ledc_update_duty(LEDC_SPEED_MODE, channel);
}

static void status_led_set(uint8_t red, uint8_t green, uint8_t blue) {
    status_led_channel_set(STATUS_LED_RED_CHANNEL, 0xFF - red);
    status_led_channel_set(STATUS_LED_GREEN_CHANNEL, 0xFF - green);
    status_led_channel_set(STATUS_LED_BLUE_CHANNEL, 0xFF - blue);
}

static void status_led_channel_fade(ledc_channel_t channel, uint8_t value, int max_fade_time_ms) {
    ledc_set_fade_with_time(LEDC_SPEED_MODE, channel, value, max_fade_time_ms);
    ledc_fade_start(LEDC_SPEED_MODE, channel, LEDC_FADE_NO_WAIT);
}

static void status_led_fade(uint8_t red, uint8_t green, uint8_t blue, int max_fade_time_ms) {
    status_led_channel_fade(STATUS_LED_RED_CHANNEL, 0xFF - red, max_fade_time_ms);
    status_led_channel_fade(STATUS_LED_GREEN_CHANNEL, 0xFF - green, max_fade_time_ms);
    status_led_channel_fade(STATUS_LED_BLUE_CHANNEL, 0xFF - blue, max_fade_time_ms);
}

static void status_led_show(status_led_handle_t color) {
    if (color->flashing_mode == STATUS_LED_STATIC) {
        status_led_set(color->red, color->green, color->blue);

        vTaskDelay(pdMS_TO_TICKS(color->duration));
    } else {
        bool fade = color->flashing_mode == STATUS_LED_FADE;
        bool active = true;
        for (unsigned int i = 0; i < color->duration / color->interval; i++, active = !active) {
            uint8_t red = active ? color->red : 0;
            uint8_t green = active ? color->green : 0;
            uint8_t blue = active ? color->blue : 0;
            if (fade) {
                status_led_fade(red, green, blue, color->interval / 2);
            } else {
                status_led_set(red, green, blue);
            }

            vTaskDelay(pdMS_TO_TICKS(color->interval));
        }
    }

    // Turn off all LEDs
    status_led_set(0, 0, 0);
}

static void status_led_task() {
    while (true) {
        // Wait for a color
        if (SLIST_EMPTY(&status_led_colors_list)) vTaskSuspend(NULL);

        status_led_handle_t color, color_tmp;
        SLIST_FOREACH_SAFE(color, &status_led_colors_list, next, color_tmp) {
            // Marked for removal
            if (color->remove) {
                SLIST_REMOVE(&status_led_colors_list, color, status_led_color_t, next);
                free(color);
                continue;
            }

            // Show color
            if (color->active) status_led_show(color);
        }
    }
}

void status_led_init() {
    ledc_timer_config_t ledc_timer = {
            .duty_resolution = LEDC_TIMER_8_BIT,
            .freq_hz = STATUS_LED_FREQ,
            .speed_mode = LEDC_SPEED_MODE,
            .timer_num = LEDC_TIMER_0,
            .clk_cfg = LEDC_AUTO_CLK,
    };

    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_config = {
            .duty = 255,
            .speed_mode = LEDC_SPEED_MODE,
            .hpoint = 0,
            .timer_sel = LEDC_TIMER_0
    };

    ledc_config.channel = STATUS_LED_RED_CHANNEL;
    ledc_config.gpio_num = STATUS_LED_RED_GPIO;
    ledc_channel_config(&ledc_config);

    ledc_config.channel = STATUS_LED_GREEN_CHANNEL;
    ledc_config.gpio_num = STATUS_LED_GREEN_GPIO;
    ledc_channel_config(&ledc_config);

    ledc_config.channel = STATUS_LED_BLUE_CHANNEL;
    ledc_config.gpio_num = STATUS_LED_BLUE_GPIO;
    ledc_channel_config(&ledc_config);

    ledc_config.channel = STATUS_LED_SLEEP_CHANNEL;
    ledc_config.gpio_num = STATUS_LED_SLEEP_GPIO;
    ledc_channel_config(&ledc_config);

    ledc_config.duty = 0;
    ledc_config.channel = STATUS_LED_RSSI_CHANNEL;
    ledc_config.gpio_num = STATUS_LED_RSSI_GPIO;
    ledc_channel_config(&ledc_config);

    ledc_config.channel = STATUS_LED_ASSOC_CHANNEL;
    ledc_config.gpio_num = STATUS_LED_ASSOC_GPIO;
    ledc_channel_config(&ledc_config);

    ledc_fade_func_install(0);

    xTaskCreate(status_led_task, "status_led", 2048, NULL, TASK_PRIORITY_STATUS_LED, &led_task);
}

void rssi_led_set(uint8_t value) {
    status_led_channel_set(STATUS_LED_RSSI_CHANNEL, value);
}

void rssi_led_fade(uint8_t value, int max_fade_time_ms) {
    status_led_channel_fade(STATUS_LED_RSSI_CHANNEL, value, max_fade_time_ms);
}

void assoc_led_set(uint8_t value) {
    status_led_channel_set(STATUS_LED_ASSOC_CHANNEL, value);
}

void assoc_led_fade(uint8_t value, int max_fade_time_ms) {
    status_led_channel_fade(STATUS_LED_ASSOC_CHANNEL, value, max_fade_time_ms);
}

void sleep_led_set(uint8_t value) {
    status_led_channel_set(STATUS_LED_SLEEP_CHANNEL, 0xFF - value);
}

void sleep_led_fade(uint8_t value, int max_fade_time_ms) {
    status_led_channel_fade(STATUS_LED_SLEEP_CHANNEL, 0xFF - value, max_fade_time_ms);
}