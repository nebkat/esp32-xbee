/* 
 * This file is part of the ESP32-XBee distribution (https://github.com/nebkat/esp32-xbee).
 * Copyright (c) 2020 Nebojsa Cvetkovic.
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

#include "include/stream_stats.h"

#include <freertos/FreeRTOS.h>

#include <sys/queue.h>
#include <freertos/task.h>
#include <tasks.h>

#define RUNNING_AVERAGE_PERIOD 1000
#define RUNNING_AVERAGE_ALPHA 0.8
#define RUNNING_AVERAGE_PERIOD_CORRECTION (1000.0 / RUNNING_AVERAGE_PERIOD)

struct stream_stats {
    const char *name;

    uint32_t total_in;
    uint32_t total_out;

    double rate_in;
    double rate_out;

    uint32_t rate_in_period_count;
    uint32_t rate_out_period_count;

    SLIST_ENTRY(stream_stats) next;
};

static SLIST_HEAD(stream_stats_list_t, stream_stats) stream_stats_list;

static void stream_stats_task(void *ctx) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(RUNNING_AVERAGE_PERIOD));
        stream_stats_handle_t stats;
        SLIST_FOREACH(stats, &stream_stats_list, next) {
            stats->rate_in = stats->rate_in * RUNNING_AVERAGE_ALPHA +
                    (double) stats->rate_in_period_count * (1.0 - RUNNING_AVERAGE_ALPHA) * RUNNING_AVERAGE_PERIOD_CORRECTION;
            stats->rate_out = stats->rate_out * RUNNING_AVERAGE_ALPHA +
                    (double) stats->rate_out_period_count * (1.0 - RUNNING_AVERAGE_ALPHA) * RUNNING_AVERAGE_PERIOD_CORRECTION;

            stats->rate_in_period_count = 0;
            stats->rate_out_period_count = 0;
        }
    }
}

void stream_stats_init() {
    SLIST_INIT(&stream_stats_list);
    xTaskCreate(stream_stats_task, "stream_stats_task", 2048, NULL, TASK_PRIORITY_STATS, NULL);
}

stream_stats_handle_t stream_stats_new(const char *name) {
    stream_stats_handle_t new = calloc(1, sizeof(struct stream_stats));
    new->name = name;
    SLIST_INSERT_HEAD(&stream_stats_list, new, next);

    return new;
}

void stream_stats_increment(stream_stats_handle_t stats, uint32_t in, uint32_t out) {
    stats->total_in += in;
    stats->total_out += out;
    stats->rate_in_period_count += in;
    stats->rate_out_period_count += out;
}

void stream_stats_values(stream_stats_handle_t stats, stream_stats_values_t *values) {
    *values = (stream_stats_values_t) {
            .name = stats->name,
            .total_in = stats->total_in,
            .total_out = stats->total_out,
            .rate_in = stats->rate_in,
            .rate_out = stats->rate_out
    };
}

stream_stats_handle_t stream_stats_first() {
    return SLIST_FIRST(&stream_stats_list);
}

stream_stats_handle_t stream_stats_next(stream_stats_handle_t stats) {
    return SLIST_NEXT(stats, next);
}

stream_stats_handle_t stream_stats_get(const char *name) {
    stream_stats_handle_t stats;
    SLIST_FOREACH(stats, &stream_stats_list, next) {
        if (stats->name == name) {
            return stats;
        }
    }

    return NULL;
}