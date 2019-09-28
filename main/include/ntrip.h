#ifndef ESP32_XBEE_NTRIP_H
#define ESP32_XBEE_NTRIP_H

#define NTRIP_PORT_DEFAULT 2101
#define NTRIP_MOUNTPOINT_DEFAULT "DEFAULT"
#define NTRIP_KEEP_ALIVE_THRESHOLD 10000

#define NEWLINE "\r\n"
#define NEWLINE_LENGTH 2

#ifndef CONFIG_EXAMPLE_IPV6
#define SOCKET_ADDR_FAMILY AF_INET
#define SOCKET_IP_PROTOCOL IPPROTO_IP
#elif
#define SOCKET_ADDR_FAMILY AF_INET6
#define SOCKET_IP_PROTOCOL IPPROTO_IP6
#endif

void ntrip_server_init();
void ntrip_client_init();
void ntrip_caster_init();

#endif //ESP32_XBEE_NTRIP_H
