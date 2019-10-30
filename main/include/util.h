#ifndef ESP32_XBEE_UTIL_H
#define ESP32_XBEE_UTIL_H

#include <esp_transport.h>

#include <sys/socket.h>

#define DEBUG( level, tag ) ESP_LOG_LEVEL(level, tag, "This is line %d of file %s (function %s)", __LINE__, __FILE__, __func__)

#define ERROR_ACTION(TAG, condition, action, format, ... ) if ((condition)) {             \
            ESP_LOGE(TAG, "%s:%d (%s): " format, __FILE__, __LINE__, __FUNCTION__,  ##__VA_ARGS__); \
            action; \
        }

#define SOCKTYPE_NAME(socktype) (socktype == SOCK_STREAM ? "TCP" : (socktype == SOCK_DGRAM ? "UDP" : (socktype == SOCK_RAW ? "RAW" : "???")))

#define CONNECT_SOCKET_ERROR_RESOLVE -2
#define CONNECT_SOCKET_ERROR_CONNECT -1

void destroy_socket(int *socket);
char *sockaddrtostr(struct sockaddr *a);

char *extract_http_header(const char *buffer, const char *key);

int connect_socket(char *host, int port, int socktype);
char *http_auth_basic_header(const char *username, const char *password);

#endif //ESP32_XBEE_UTIL_H
