/**
 * @file test_esphome_basic.c
 * @brief ESPHome Noise handshake test using noise-c library
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

#include "noise/protocol.h"
#include "noise/protocol/cipherstate.h"
#include "noise/protocol/handshakestate.h"
#include "noise/protocol/signstate.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <host> [port]\n", argv[0]);
        printf("Example: %s 192.168.69.179 6053\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = (argc > 2) ? atoi(argv[2]) : 6053;

    printf("Testing ESPHome Noise handshake with %s:%d\n", host, port);

    // Initialize libsodium
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    // Connect
    printf("Connecting...\n");
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    printf("Connected successfully!\n");

    // Initialize Noise handshake
    if (noise_init() != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to initialize noise library\n");
        close(sock);
        return 1;
    }

    NoiseHandshakeState *handshake;
    int err = noise_handshakestate_new_by_name(&handshake, "Noise_NNpsk0_25519_ChaChaPoly_SHA256", NOISE_ROLE_INITIATOR);
    if (err != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to create handshake state: %d\n", err);
        close(sock);
        return 1;
    }

    // Set prologue
    const char *prologue = "NoiseAPIInit\x00\x00";
    err = noise_handshakestate_set_prologue(handshake, (const uint8_t *)prologue, strlen(prologue));
    if (err != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to set prologue: %d\n", err);
        noise_handshakestate_free(handshake);
        close(sock);
        return 1;
    }

    // Set up PSK (decode from base64)
    const char *psk_b64 = "93lQ8xeyF152/qDFEOngsSIJ74BrNRIOvXPVs8yNOIQ=";
    uint8_t psk[32];
    size_t psk_len;
    if (sodium_base642bin(psk, sizeof(psk), psk_b64, strlen(psk_b64), NULL, &psk_len, NULL, sodium_base64_VARIANT_ORIGINAL) != 0 || psk_len != 32) {
        fprintf(stderr, "Failed to decode PSK\n");
        noise_handshakestate_free(handshake);
        close(sock);
        return 1;
    }

    err = noise_handshakestate_set_pre_shared_key(handshake, psk, 32);
    if (err != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to set PSK: %d\n", err);
        noise_handshakestate_free(handshake);
        close(sock);
        return 1;
    }

    // Start handshake
    err = noise_handshakestate_start(handshake);
    if (err != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to start handshake: %d\n", err);
        noise_handshakestate_free(handshake);
        close(sock);
        return 1;
    }

    // Start handshake - send e (ephemeral key)
    NoiseBuffer m1;
    uint8_t message1[1024];
    noise_buffer_set_output(m1, message1, sizeof(message1));
    NoiseBuffer payload1;
    uint8_t payload_data1[1] = {0}; // Empty payload
    noise_buffer_set_input(payload1, payload_data1, 0);
    err = noise_handshakestate_write_message(handshake, &m1, &payload1);
    if (err != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to write handshake message 1: %d\n", err);
        noise_handshakestate_free(handshake);
        close(sock);
        return 1;
    }

    printf("Sending handshake message 1 (%d bytes)...\n", m1.size);
    for (size_t i = 0; i < m1.size && i < 64; i++) {
        printf("%02x ", message1[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    if (send(sock, message1, m1.size, 0) != (ssize_t)m1.size) {
        perror("send handshake message 1");
        noise_handshakestate_free(handshake);
        close(sock);
        return 1;
    }

    // Read response
    uint8_t response[1024];
    ssize_t received = recv(sock, response, sizeof(response), 0);
    if (received < 0) {
        perror("recv");
        noise_handshakestate_free(handshake);
        close(sock);
        return 1;
    }

    printf("Received %zd bytes:\n", received);
    for (ssize_t i = 0; i < received && i < 64; i++) {
        printf("%02x ", response[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    // Process handshake response
    NoiseBuffer m2;
    noise_buffer_set_input(m2, response, received);
    NoiseBuffer payload2;
    uint8_t payload_data2[1024];
    noise_buffer_set_output(payload2, payload_data2, sizeof(payload_data2));
    err = noise_handshakestate_read_message(handshake, &m2, &payload2);
    if (err != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to read handshake message 2: %d\n", err);
        noise_handshakestate_free(handshake);
        close(sock);
        return 1;
    }

    printf("Handshake completed successfully!\n");

    // Get the transport cipherstates
    NoiseCipherState *send_cipher, *recv_cipher;
    err = noise_handshakestate_split(handshake, &send_cipher, &recv_cipher);
    if (err != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to split handshake: %d\n", err);
        noise_handshakestate_free(handshake);
        close(sock);
        return 1;
    }

    noise_handshakestate_free(handshake);

    // Now we can send encrypted messages
    // Send HelloRequest
    uint8_t hello_data[] = {0x01, 0x08, 0x01, 0x10, 0x0e, 0x1a, 0x0d, 'a', 'i', 'o', 'e', 's', 'p', 'h', 'o', 'm', 'e', 'a', 'p', 'i'};
    NoiseBuffer hello_msg;
    uint8_t hello_encrypted[1024];
    noise_buffer_set_inout(hello_msg, hello_encrypted, sizeof(hello_encrypted), sizeof(hello_data));
    memcpy(hello_msg.data, hello_data, sizeof(hello_data));
    err = noise_cipherstate_encrypt(send_cipher, &hello_msg);
    if (err != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to encrypt hello: %d\n", err);
        noise_cipherstate_free(send_cipher);
        noise_cipherstate_free(recv_cipher);
        close(sock);
        return 1;
    }

    // Send as ESPHome frame
    uint8_t hello_frame[1024];
    hello_frame[0] = 0x01; // encrypted preamble
    uint16_t hello_len = hello_msg.size;
    hello_frame[1] = (hello_len >> 8) & 0xFF;
    hello_frame[2] = hello_len & 0xFF;
    memcpy(hello_frame + 3, hello_encrypted, hello_len);

    printf("Sending HelloRequest (%d bytes)...\n", 3 + hello_len);
    if (send(sock, hello_frame, 3 + hello_len, 0) != (ssize_t)(3 + hello_len)) {
        perror("send hello");
        noise_cipherstate_free(send_cipher);
        noise_cipherstate_free(recv_cipher);
        close(sock);
        return 1;
    }

    // Read HelloResponse
    received = recv(sock, response, sizeof(response), 0);
    if (received < 0) {
        perror("recv hello response");
        noise_cipherstate_free(send_cipher);
        noise_cipherstate_free(recv_cipher);
        close(sock);
        return 1;
    }

    printf("Received HelloResponse (%zd bytes):\n", received);
    for (ssize_t i = 0; i < received && i < 64; i++) {
        printf("%02x ", response[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    if (received >= 3) {
        uint8_t preamble = response[0];
        uint16_t length = (response[1] << 8) | response[2];

        if (preamble == 0x01 && received >= 3 + length) {
            // Decrypt the message
            NoiseBuffer decrypted;
            uint8_t decrypted_data[1024];
            noise_buffer_set_input(decrypted, response + 3, length);
            err = noise_cipherstate_decrypt(recv_cipher, &decrypted);
            if (err != NOISE_ERROR_NONE) {
                fprintf(stderr, "Failed to decrypt response: %d\n", err);
            } else {
                printf("Decrypted response (%d bytes):\n", decrypted.size);
                for (size_t i = 0; i < decrypted.size && i < 64; i++) {
                    printf("%02x ", decrypted.data[i]);
                    if ((i + 1) % 16 == 0) printf("\n");
                }
                printf("\n");
            }
        }
    }

    noise_cipherstate_free(send_cipher);
    noise_cipherstate_free(recv_cipher);
    close(sock);
    printf("Test completed successfully!\n");
    return 0;
}