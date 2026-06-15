/**
 * @file test_client.c
 * @brief Simple test client for ESPHome server
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sodium.h>

// Include the ESPHome API headers
#include "components/esphome_api/esphome_api_protocol.h"

int esphome_connect_regular(int socket_fd, const char *password) {
    uint8_t buffer[1024];
    uint32_t msg_type;
    int result;

    // Send regular Hello
    if (esphome_send_hello(socket_fd) != 0) {
        printf("Failed to send hello\n");
        return -1;
    }

    // Read Hello response
    result = esphome_read_message(socket_fd, buffer, sizeof(buffer), &msg_type);
    if (result < 0 || msg_type != ESPHOME_MSG_HELLO_RESPONSE) {
        printf("Failed to read hello response (type: %u, error: %d)\n", msg_type, result);
        if (result > 0) {
            printf("Payload: ");
            for (int i = 0; i < result && i < 32; i++) {
                printf("%02x ", buffer[i]);
            }
            printf("\n");
        }
        return -1;
    }

    printf("Received Hello response\n");

    // Send Connect request
    if (esphome_send_connect(socket_fd, password) != 0) {
        printf("Failed to send connect\n");
        return -1;
    }

    // Read Connect response
    result = esphome_read_message(socket_fd, buffer, sizeof(buffer), &msg_type);
    if (result < 0 || msg_type != ESPHOME_MSG_CONNECT_RESPONSE) {
        printf("Failed to read connect response (type: %u, error: %d)\n", msg_type, result);
        return -1;
    }

    printf("Regular ESPHome connection successful!\n");
    return 0;
}

int esphome_connect(int socket_fd, const uint8_t *psk, const char *password) {
    noise_handshake_t handshake;
    uint8_t buffer[1024];
    size_t buffer_len;
    uint32_t msg_type;
    int result;

    // Initialize Noise handshake
    if (noise_handshake_init(&handshake, psk) != 0) {
        printf("Failed to initialize handshake\n");
        return -1;
    }

    // Send Noise Hello (client hello)
    result = noise_create_client_hello(&handshake, buffer, &buffer_len);
    if (result != 0) {
        printf("Failed to create client hello\n");
        return -1;
    }

    // Send the hello message
    if (esphome_send_hello_with_noise(socket_fd, buffer, buffer_len) != 0) {
        printf("Failed to send client hello\n");
        return -1;
    }

    // Read server hello response
    uint8_t preamble;
    ssize_t recv_result = recv(socket_fd, &preamble, 1, MSG_PEEK);
    if (recv_result != 1) {
        printf("Failed to peek preamble: %zd\n", recv_result);
        return -1;
    }
    printf("Server response preamble: 0x%02x\n", preamble);

    result = esphome_read_message(socket_fd, buffer, sizeof(buffer), &msg_type);
    if (result < 0) {
        printf("Failed to read server hello (error %d)\n", result);
        return -1;
    }

    printf("Received message type: %u, length: %d\n", msg_type, result);
    printf("Payload: ");
    for (int i = 0; i < result && i < 32; i++) {
        printf("%02x ", buffer[i]);
    }
    printf("\n");
    
    if (msg_type != ESPHOME_MSG_NOISE_HANDSHAKE) {
        printf("Expected Noise handshake message (%d), got %u\n", ESPHOME_MSG_NOISE_HANDSHAKE, msg_type);
        return -1;
    }

    // Process server hello (skip message type varint)
    const uint8_t *server_hello_data = buffer + _decode_varint(buffer, &msg_type);
    size_t server_hello_len = result - (server_hello_data - buffer);

    result = noise_process_server_hello(&handshake, server_hello_data, server_hello_len);
    if (result != 0) {
        printf("Failed to process server hello\n");
        return -1;
    }

    // Send client auth (empty for NNpsk0)
    result = noise_create_client_auth(&handshake, buffer, &buffer_len);
    if (result != 0) {
        printf("Failed to create client auth\n");
        return -1;
    }

    // Send encrypted client auth message
    result = esphome_send_encrypted_message(socket_fd, &handshake, ESPHOME_MSG_NOISE_HANDSHAKE, buffer, buffer_len);
    if (result != 0) {
        printf("Failed to send client auth\n");
        return -1;
    }

    // Read server auth response
    result = esphome_read_encrypted_message(socket_fd, &handshake, buffer, sizeof(buffer), &msg_type);
    if (result < 0) {
        printf("Failed to read server auth\n");
        return -1;
    }

    if (msg_type != ESPHOME_MSG_NOISE_HANDSHAKE) {
        printf("Expected Noise handshake message, got %u\n", msg_type);
        return -1;
    }

    // Process server auth (empty for NNpsk0)
    result = noise_process_client_auth(&handshake, psk, buffer, result);
    if (result != 0) {
        printf("Failed to process server auth\n");
        return -1;
    }

    printf("Noise handshake completed successfully!\n");

    // Now send the regular ESPHome hello
    if (esphome_send_hello(socket_fd) != 0) {
        printf("Failed to send hello\n");
        return -1;
    }

    // Read hello response
    result = esphome_read_encrypted_message(socket_fd, &handshake, buffer, sizeof(buffer), &msg_type);
    if (result < 0 || msg_type != ESPHOME_MSG_HELLO_RESPONSE) {
        printf("Failed to read hello response\n");
        return -1;
    }

    // Send connect request
    if (esphome_send_connect(socket_fd, password) != 0) {
        printf("Failed to send connect\n");
        return -1;
    }

    // Read connect response
    result = esphome_read_encrypted_message(socket_fd, &handshake, buffer, sizeof(buffer), &msg_type);
    if (result < 0 || msg_type != ESPHOME_MSG_CONNECT_RESPONSE) {
        printf("Failed to read connect response\n");
        return -1;
    }

    printf("ESPHome connection established successfully!\n");
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <host> <port> <psk_hex>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *psk_hex = argv[3];

    printf("Testing ESPHome connection to %s:%d\n", host, port);

    // Initialize libsodium
    if (sodium_init() < 0) {
        printf("ERROR: Failed to initialize libsodium\n");
        return 1;
    }

    // Convert PSK from Base64
    uint8_t psk[32];
    if (strchr(psk_hex, '=') != NULL || strlen(psk_hex) > 50) {  // Assume Base64 if contains = or longer than hex
        // Base64 decode
        size_t decoded_len;
        if (sodium_base642bin(psk, 32, psk_hex, strlen(psk_hex), NULL, &decoded_len, NULL, sodium_base64_VARIANT_ORIGINAL) != 0 || decoded_len != 32) {
            printf("PSK Base64 decode failed, trying URL-safe variant\n");
            if (sodium_base642bin(psk, 32, psk_hex, strlen(psk_hex), NULL, &decoded_len, NULL, sodium_base64_VARIANT_URLSAFE) != 0 || decoded_len != 32) {
                printf("PSK must be valid Base64 encoding 32 bytes\n");
                return 1;
            }
        }
        printf("Decoded %zu bytes from Base64 PSK\n", decoded_len);
    } else {
        // Hex decode (fallback)
        if (strlen(psk_hex) != 64) {
            printf("PSK must be 64 hex characters or valid Base64\n");
            return 1;
        }
        for (int i = 0; i < 32; i++) {
            char hex[3] = {psk_hex[i*2], psk_hex[i*2+1], 0};
            psk[i] = (uint8_t)strtol(hex, NULL, 16);
        }
    }

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Failed to create socket\n");
        return 1;
    }

    // Connect
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &server_addr.sin_addr);

    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 5;  // 5 second timeout
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    printf("Connecting...\n");
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Connection failed: %s\n", strerror(errno));
        close(sock);
        return 1;
    }

    printf("Connected successfully\n");

    // Test the connection using the ESPHome API
    printf("Device requires encryption, trying Noise protocol...\n");
    int result = esphome_connect(sock, psk, "49trahreg48");
    if (result == 0) {
        printf("ESPHome Noise connection test PASSED\n");
    } else {
        printf("ESPHome Noise connection test FAILED (error %d)\n", result);
    }

    close(sock);
    return result;
}