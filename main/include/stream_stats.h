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

#ifndef ESP32_XBEE_STREAM_STATS_H
#define ESP32_XBEE_STREAM_STATS_H

#include <stdint.h>

typedef struct stream_stats_values {
    const char *name;

    uint32_t total_in;
    uint32_t total_out;

    uint32_t rate_in;
    uint32_t rate_out;
} stream_stats_values_t;

typedef struct stream_stats *stream_stats_handle_t;

void stream_stats_init();
stream_stats_handle_t stream_stats_new(const char *name);

void stream_stats_increment(stream_stats_handle_t stats, uint32_t in, uint32_t out);
void stream_stats_values(stream_stats_handle_t stats, stream_stats_values_t *values);

stream_stats_handle_t stream_stats_first();
stream_stats_handle_t stream_stats_next(stream_stats_handle_t stats);

#endif //ESP32_XBEE_STREAM_STATS_H
