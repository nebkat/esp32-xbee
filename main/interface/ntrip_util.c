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
#include <stdbool.h>

static bool str_starts_with(const char *a, const char *b) {
    return strncmp(a, b, strlen(b)) == 0;
}

bool ntrip_response_ok(void *response) {
    return str_starts_with(response, "OK") || str_starts_with(response, "ICY 200 OK") ||
           str_starts_with(response, "HTTP/1.1 200 OK");
}

bool ntrip_response_sourcetable_ok(void *response) {
    return str_starts_with(response, "HTTP/1.1 200 OK") || str_starts_with(response, "SOURCETABLE 200 OK");
}