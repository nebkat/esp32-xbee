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

#define LEDC_SPEED_MODE LEDC_HIGH_SPEED_MODE

#define STATUS_LED_RED_GPIO GPIO_NUM_21
#define STATUS_LED_GREEN_GPIO GPIO_NUM_22
#define STATUS_LED_BLUE_GPIO GPIO_NUM_23
#define STATUS_LED_FREQUENCY 10000

status_led_handle_t colors;
SemaphoreHandle_t colors_semaphore;

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

    if (colors != NULL) colors->prev = color;
    color->next = colors;

    colors = color;

    xSemaphoreGive(colors_semaphore);

    return color;
}

void status_led_remove(status_led_handle_t color) {
    if (color == NULL) return;
    color->remove = true;
}

static void status_led_show(status_led_handle_t color) {
    switch (color->flashing_mode) {
        case STATUS_LED_STATIC: {
            ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_0, 0xFF - color->red);
            ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_1, 0xFF - color->green);
            ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_2, 0xFF - color->blue);
            ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_0);
            ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_1);
            ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_2);

            vTaskDelay(pdMS_TO_TICKS(color->duration));
            break;
        }
        case STATUS_LED_FADE: {
            bool active = true;
            for (unsigned int i = 0; i < color->duration / color->interval; i++) {
                ledc_set_fade_with_time(LEDC_SPEED_MODE, LEDC_CHANNEL_0, 0xFF - (active ? color->red : 0), color->interval / 2);
                ledc_set_fade_with_time(LEDC_SPEED_MODE, LEDC_CHANNEL_1, 0xFF - (active ? color->green : 0), color->interval / 2);
                ledc_set_fade_with_time(LEDC_SPEED_MODE, LEDC_CHANNEL_2, 0xFF - (active ? color->blue : 0), color->interval / 2);
                ledc_fade_start(LEDC_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);
                ledc_fade_start(LEDC_SPEED_MODE, LEDC_CHANNEL_1, LEDC_FADE_NO_WAIT);
                ledc_fade_start(LEDC_SPEED_MODE, LEDC_CHANNEL_2, LEDC_FADE_NO_WAIT);

                vTaskDelay(pdMS_TO_TICKS(color->interval));

                active = !active;
            }
            break;
        }

        case STATUS_LED_BLINK: {
            bool active = true;
            for (unsigned int i = 0; i < color->duration / color->interval; i++) {
                ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_0, 0xFF - (active ? color->red : 0));
                ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_1, 0xFF - (active ? color->green : 0));
                ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_2, 0xFF - (active ? color->blue : 0));
                ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_0);
                ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_1);
                ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_2);

                vTaskDelay(pdMS_TO_TICKS(color->interval));

                active = !active;
            }
            break;
        }
    }

    // Turn off all LEDs
    ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_0, 0xFF);
    ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_1, 0xFF);
    ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_2, 0xFF);
    ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_1);
    ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_2);
}

static void status_led_task() {
    status_led_handle_t color = NULL;
    while (true) {
        // Wait for a color
        if (colors == NULL) xSemaphoreTake(colors_semaphore, 0);

        // Start or loop around
        if (color == NULL) color = colors;

        // Marked for removal
        if (color->remove) {
            if (color->prev != NULL) color->prev->next = color->next;
            if (color->next != NULL) color->next->prev = color->prev;
            if (colors == color) colors = color->next;

            status_led_handle_t next = color->next;
            free(color);
            color = next;

            continue;
        }

        // Show color
        if (color->active) status_led_show(color);

        // Next color
        color = color->next;
    }
}

void status_led_init() {
    colors_semaphore = xSemaphoreCreateBinary();

    ledc_timer_config_t ledc_timer = {
            .duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
            .freq_hz = 1000,                      // frequency of PWM signal
            .speed_mode = LEDC_HIGH_SPEED_MODE,           // timer mode
            .timer_num = LEDC_TIMER_0,            // timer index
            .clk_cfg = LEDC_AUTO_CLK,              // Auto select the source clock
    };

    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel_red = {
            .channel    = LEDC_CHANNEL_0,
            .duty       = 255,
            .gpio_num   = STATUS_LED_RED_GPIO,
            .speed_mode = LEDC_SPEED_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_TIMER_0
    };

    ledc_channel_config_t ledc_channel_green = {
            .channel    = LEDC_CHANNEL_1,
            .duty       = 255,
            .gpio_num   = STATUS_LED_GREEN_GPIO,
            .speed_mode = LEDC_SPEED_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_TIMER_0
    };

    ledc_channel_config_t ledc_channel_blue = {
            .channel    = LEDC_CHANNEL_2,
            .duty       = 255,
            .gpio_num   = STATUS_LED_BLUE_GPIO,
            .speed_mode = LEDC_SPEED_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_TIMER_0
    };

    ledc_channel_config(&ledc_channel_red);
    ledc_channel_config(&ledc_channel_green);
    ledc_channel_config(&ledc_channel_blue);

    ledc_fade_func_install(0);

    xTaskCreate(status_led_task, "status_led", 2048, NULL, TASK_PRIORITY_STATUS_LED, NULL);
}