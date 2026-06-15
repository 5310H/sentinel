/**
 * @file test_simple_esphome.c
 * @brief Simple ESPHome handshake test using working sequence
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

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <host> [port]\n", argv[0]);
        printf("Example: %s 192.168.69.178 6053\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = (argc > 2) ? atoi(argv[2]) : 6053;

    printf("Testing ESPHome handshake with %s:%d\n", host, port);

    // Initialize libsodium
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }

    // Decode PSK
    const char *psk_b64 = "93lQ8xeyF152/qDFEOngsSIJ74BrNRIOvXPVs8yNOIQ=";
    uint8_t psk[32];
    size_t psk_len;
    if (sodium_base642bin(psk, sizeof(psk), psk_b64, strlen(psk_b64), NULL, &psk_len, NULL, sodium_base64_VARIANT_ORIGINAL) != 0 || psk_len != 32) {
        fprintf(stderr, "Failed to decode PSK\n");
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

    // ESPHome Noise handshake sequence (from working C++ implementation)
    // Step 1: Send protocol identifier frame
    uint8_t protocol_frame[] = {0x01, 0x00, 0x00, 0x01};
    printf("Sending protocol identifier frame...\n");
    if (send(sock, protocol_frame, sizeof(protocol_frame), 0) != sizeof(protocol_frame)) {
        perror("send protocol frame");
        close(sock);
        return 1;
    }

    // Step 2: Send ephemeral public key (32 bytes)
    uint8_t e_priv[32], e_pub[32];
    memset(e_priv, 0x42, 32);  // Fixed key for testing
    crypto_scalarmult_base(e_pub, e_priv);

    printf("Sending ephemeral public key...\n");
    if (send(sock, e_pub, 32, 0) != 32) {
        perror("send ephemeral key");
        close(sock);
        return 1;
    }

    // Read server response (should be encrypted device info)
    uint8_t response[1024];
    ssize_t received = recv(sock, response, sizeof(response), 0);
    if (received < 0) {
        perror("recv server response");
        close(sock);
        return 1;
    }

    printf("Received server response (%zd bytes):\n", received);
    for (ssize_t i = 0; i < received && i < 64; i++) {
        printf("%02x ", response[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    if (received >= 3) {
        uint8_t preamble = response[0];
        uint16_t length = (response[1] << 8) | response[2];

        printf("Preamble: 0x%02x, Length: %u\n", preamble, length);

        if (preamble == 0x01 && received >= 3 + length) {
            printf("Received encrypted server response\n");
            // This should be the server's ephemeral key encrypted
        } else {
            printf("Unexpected response format\n");
        }
    }

    close(sock);
    printf("Handshake test completed!\n");
    return 0;
}