/**
 * @file esphome_api_server.h
 * @brief ESPHome Native API server implementation
 */

#ifndef ESPHOME_API_SERVER_H
#define ESPHOME_API_SERVER_H

#include "esphome_api_protocol.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Server configuration
typedef struct {
    uint16_t port;
    const char *device_name;
    uint8_t psk[32];  // 32-byte pre-shared key for Noise protocol
} esphome_server_config_t;

// Server instance
typedef struct {
    int listen_socket;
    esphome_server_config_t config;
    bool running;
} esphome_server_t;

// Server functions
int esphome_server_init(esphome_server_t *server, const esphome_server_config_t *config);
int esphome_server_start(esphome_server_t *server);
void esphome_server_stop(esphome_server_t *server);

// Connection handling (internal)
void *esphome_server_handle_connection(void *arg);

#ifdef __cplusplus
}
#endif

#endif // ESPHOME_API_SERVER_H