/**
 * @file esphome_api_server.c
 * @brief ESPHome Native API server implementation
 */

#include "esphome_api_server.h"
#include <string.h>
#include <stdio.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG = "ESPHOME_SRV";
#else
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sodium.h>
#define TAG "ESPHOME_SRV"
#endif

// Connection context
typedef struct {
    int socket_fd;
    esphome_server_t *server;
    noise_handshake_t handshake;
    bool handshake_complete;
} connection_ctx_t;

/**
 * @brief Initialize ESPHome server
 */
int esphome_server_init(esphome_server_t *server, const esphome_server_config_t *config) {
    memset(server, 0, sizeof(esphome_server_t));
    server->config = *config;

    // Initialize libsodium if not already done
    if (sodium_init() < 0) {
        printf("Failed to initialize libsodium\n");
        return -1;
    }

    return 0;
}

/**
 * @brief Start ESPHome server
 */
int esphome_server_start(esphome_server_t *server) {
    struct sockaddr_in server_addr;

    // Create listening socket
    server->listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_socket < 0) {
        printf("Failed to create server socket\n");
        return -1;
    }

    // Set socket options
    int opt = 1;
    setsockopt(server->listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind to port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server->config.port);

    if (bind(server->listen_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Failed to bind server socket\n");
        close(server->listen_socket);
        return -1;
    }

    // Listen
    if (listen(server->listen_socket, 5) < 0) {
        printf("Failed to listen on server socket\n");
        close(server->listen_socket);
        return -1;
    }

    server->running = true;
    printf("ESPHome server listening on port %d\n", server->config.port);

    // Accept connections in a loop
    while (server->running) {
        printf("Waiting for connection...\n");
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_socket = accept(server->listen_socket, (struct sockaddr*)&client_addr, &client_len);
        printf("accept() returned %d\n", client_socket);
        if (client_socket < 0) {
            if (server->running) {
                printf("Failed to accept connection\n");
            }
            continue;
        }

        printf("Accepted connection from client\n");

        // Handle connection in new thread/task
        connection_ctx_t *ctx = malloc(sizeof(connection_ctx_t));
        if (ctx) {
            memset(ctx, 0, sizeof(connection_ctx_t));
            ctx->socket_fd = client_socket;
            ctx->server = server;

            printf("Creating thread for connection...\n");
#ifdef ESP_PLATFORM
            xTaskCreate(esphome_server_handle_connection, "esphome_conn", 4096, ctx, 5, NULL);
#else
            pthread_t thread;
            int ret = pthread_create(&thread, NULL, esphome_server_handle_connection, ctx);
            printf("pthread_create returned %d\n", ret);
            if (ret == 0) {
                pthread_detach(thread);
                printf("Thread created successfully\n");
            } else {
                printf("Failed to create thread\n");
                free(ctx);
                close(client_socket);
            }
#endif
        } else {
            printf("Failed to allocate context\n");
            close(client_socket);
        }
    }

    return 0;
}

/**
 * @brief Stop ESPHome server
 */
void esphome_server_stop(esphome_server_t *server) {
    server->running = false;
    if (server->listen_socket >= 0) {
        close(server->listen_socket);
        server->listen_socket = -1;
    }
}

/**
 * @brief Handle client connection
 */
void *esphome_server_handle_connection(void *arg) {
    connection_ctx_t *ctx = (connection_ctx_t *)arg;
    uint8_t buffer[1024];
    uint32_t message_type;

    printf("Handling new connection\n");

    // Initialize handshake
    printf("About to initialize handshake\n");
    noise_handshake_init(&ctx->handshake, ctx->server->config.psk);
    ctx->handshake.state = NOISE_STATE_INIT;  // Server starts in INIT state
    printf("Handshake initialized\n");

    while (ctx->server->running) {
        // Read message
        printf("About to read message...\n");
        
        // First check if this is a raw Noise message (preamble 0x01 with no message type)
        uint8_t preamble;
        int peek_result = recv(ctx->socket_fd, &preamble, 1, MSG_PEEK);
        printf("Peek result: %d, preamble: 0x%02x, handshake_complete: %d\n", peek_result, preamble, ctx->handshake_complete);
        if (peek_result == 1 && preamble == 0x01 && !ctx->handshake_complete) {
            // This is likely a raw Noise message, read it directly
            printf("Detected raw Noise message (preamble 0x01)\n");
            
            // Read preamble
            uint8_t preamble_byte;
            if (recv(ctx->socket_fd, &preamble_byte, 1, 0) != 1) {
                printf("Failed to read preamble\n");
                break;
            }
            
            // Read length (2 bytes big-endian)
            uint8_t len_bytes[2];
            if (recv(ctx->socket_fd, len_bytes, 2, 0) != 2) {
                printf("Failed to read length\n");
                break;
            }
            uint16_t msg_len = (len_bytes[0] << 8) | len_bytes[1];
            
            // Read the raw Noise data
            uint8_t noise_buffer[256];
            if (msg_len > sizeof(noise_buffer)) {
                printf("Noise message too large\n");
                break;
            }
            if (recv(ctx->socket_fd, noise_buffer, msg_len, 0) != msg_len) {
                printf("Failed to read Noise data\n");
                break;
            }
            
            printf("Read raw Noise message, length %d\n", msg_len);
            
            // Handle based on handshake state
            if (ctx->handshake.state == NOISE_STATE_INIT) {
                // This should be client hello
                if (noise_process_client_hello(&ctx->handshake, noise_buffer, msg_len) == 0) {
                    printf("Processed client hello\n");

                    // Create server hello response
                    uint8_t server_hello[256];
                    size_t server_hello_len = sizeof(server_hello);
                    if (noise_create_server_hello(&ctx->handshake, ctx->server->config.psk, server_hello, &server_hello_len) == 0) {
                        // Send server hello as framed raw message: 0x01 + 2-byte big-endian length + data
                        uint8_t framed_server_hello[256 + 3];
                        framed_server_hello[0] = 0x01;
                        framed_server_hello[1] = (server_hello_len >> 8) & 0xFF;
                        framed_server_hello[2] = server_hello_len & 0xFF;
                        memcpy(&framed_server_hello[3], server_hello, server_hello_len);
                        
                        if (send(ctx->socket_fd, framed_server_hello, server_hello_len + 3, 0) == server_hello_len + 3) {
                            printf("Sent server hello (%zu bytes)\n", server_hello_len + 3);
                        } else {
                            printf("Failed to send server hello\n");
                            break;
                        }
                    } else {
                        printf("Failed to create server hello\n");
                        break;
                    }
                } else {
                    printf("Failed to process client hello\n");
                    break;
                }
            } else if (ctx->handshake.state == NOISE_STATE_SERVER_HELLO_SENT) {
                // This should be client auth
                if (noise_process_client_auth(&ctx->handshake, ctx->server->config.psk, noise_buffer, msg_len) == 0) {
                    printf("Processed client auth, handshake complete\n");
                    ctx->handshake_complete = true;
                    ctx->handshake.state = NOISE_STATE_READY;
                } else {
                    printf("Failed to process client auth\n");
                    break;
                }
            } else {
                printf("Unexpected Noise message in state %d\n", ctx->handshake.state);
                break;
            }
            continue;
        } else if (peek_result == 1 && preamble == 0x00) {
            // Unknown preamble, consume it
            printf("Unknown preamble 0x%02x, consuming\n", preamble);
            recv(ctx->socket_fd, &preamble, 1, 0);
            continue;
        }
        
        // Normal ESPHome message processing
        int len = esphome_read_message(ctx->socket_fd, buffer, sizeof(buffer), &message_type);
        printf("esphome_read_message returned %d\n", len);
        if (len < 0) {
            printf("Failed to read message or connection closed (len=%d)\n", len);
            break;
        }

        printf("Received message type %u, length %d\n", message_type, len);

        printf("Entering switch statement\n");
        switch (message_type) {
            case ESPHOME_MSG_HELLO_REQUEST: {
                printf("Processing HELLO_REQUEST\n");
                // Handle hello request
                esphome_hello_request_t request;
                if (esphome_decode_message(buffer, len, &message_type, &request) == 0) {
                    printf("Hello from client: %s\n", request.client_info);

                    // Send hello response with encryption required
                    esphome_hello_response_t response = {
                        .api_version_major = 1,
                        .api_version_minor = 9,
                        .server_info = "",
                        .name = ""
                    };
                    strcpy(response.server_info, "ESPHome");
                    strcpy(response.name, ctx->server->config.device_name);

                    uint8_t response_buffer[256];
                    int pos = 0;
                    
                    // Preamble
                    response_buffer[pos++] = 0x00;
                    
                    // Message type
                    int msg_type_pos = pos;
                    pos += _encode_varint(&response_buffer[pos], ESPHOME_MSG_HELLO_RESPONSE);
                    int msg_type_len = pos - msg_type_pos;
                    
                    // Length placeholder
                    uint8_t length_buf[5];
                    int length_len = _encode_varint(length_buf, 0);
                    
                    // Move message type after length
                    memmove(&response_buffer[1 + length_len], &response_buffer[1], msg_type_len);
                    pos = 1 + length_len + msg_type_len;
                    
                    // API version major (field 1)
                    response_buffer[pos++] = 0x08;  // Field 1, varint
                    pos += _encode_varint(&response_buffer[pos], 1);
                    
                    // API version minor (field 2)
                    response_buffer[pos++] = 0x10;  // Field 2, varint
                    pos += _encode_varint(&response_buffer[pos], 9);
                    
                    // Server info (field 3, empty string)
                    response_buffer[pos++] = 0x1A;  // Field 3, length-delimited
                    pos += _encode_varint(&response_buffer[pos], 0);
                    
                    // Name (field 4)
                    response_buffer[pos++] = 0x22;  // Field 4, length-delimited
                    const char *name = ctx->server->config.device_name;
                    int name_len = strlen(name);
                    pos += _encode_varint(&response_buffer[pos], name_len);
                    memcpy(&response_buffer[pos], name, name_len);
                    pos += name_len;
                    
                    // Calculate data length
                    uint32_t data_len = pos - (1 + length_len + msg_type_len);
                    length_len = _encode_varint(&response_buffer[1], data_len);
                    
                    if (send(ctx->socket_fd, response_buffer, pos, 0) == pos) {
                        printf("Sent hello response\n");
                    }
                } else {
                    printf("Failed to decode hello request\n");
                }
                break;
            }

            case ESPHOME_MSG_CONNECT_REQUEST: {
                // Handle connect request (should be encrypted)
                if (!ctx->handshake_complete) {
                    printf("Received connect request but handshake not complete\n");
                    break;
                }

                printf("Connect request received, handshake complete\n");
                // For testing, just acknowledge
                break;
            }

            default:
                printf("Unhandled message type %u\n", message_type);
                break;
        }
        continue;
        } else {
            // Unknown preamble, consume it
            printf("Unknown preamble 0x%02x, consuming\n", preamble);
            recv(ctx->socket_fd, &preamble, 1, 0);
            continue;
        }
        } else if (peek_result < 0) {
            printf("Peek failed or connection closed\n");
            break;
        }
    }

    // Cleanup
    noise_handshake_free(&ctx->handshake);
    close(ctx->socket_fd);
    free(ctx);

#ifdef ESP_PLATFORM
    vTaskDelete(NULL);
#endif

    return NULL;
}