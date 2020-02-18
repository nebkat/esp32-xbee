#ifndef ESP32_XBEE_NTRIP_H
#define ESP32_XBEE_NTRIP_H

#define NTRIP_GENERIC_NAME "ESP32_XBee_Ntrip"
#define NTRIP_CLIENT_NAME NTRIP_GENERIC_NAME "_Client"
#define NTRIP_SERVER_NAME NTRIP_GENERIC_NAME "_Server"
#define NTRIP_CASTER_NAME NTRIP_GENERIC_NAME "_Caster"

#define NTRIP_PORT_DEFAULT 2101
#define NTRIP_MOUNTPOINT_DEFAULT "DEFAULT"
#define NTRIP_KEEP_ALIVE_THRESHOLD 9000
#define NTRIP_KEEP_ALIVE_MESSAGE "\xD3\x00\x00\x47\xEA\x4B"

#define NEWLINE "\r\n"
#define NEWLINE_LENGTH 2

void ntrip_server_init();
void ntrip_client_init();
void ntrip_caster_init();

bool ntrip_response_ok(void *response);
bool ntrip_response_sourcetable_ok(void *response);

#endif //ESP32_XBEE_NTRIP_H
