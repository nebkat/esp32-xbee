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

#ifndef ESP32_XBEE_RETRY_H
#define ESP32_XBEE_RETRY_H

#include <stdint.h>

typedef struct retry_delay *retry_delay_handle_t;

retry_delay_handle_t retry_init(bool first_instant, uint8_t short_count, int short_delay, int max_delay);
int retry_delay(retry_delay_handle_t handle);
void retry_reset(retry_delay_handle_t handle);

#endif //ESP32_XBEE_RETRY_H
