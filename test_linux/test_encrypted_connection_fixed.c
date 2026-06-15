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
#include "../components/esphome_api/esphome_api_protocol.h"

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <host> <port> <psk_hex>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *psk_hex = argv[3];

    printf("Testing ESPHome encrypted connection to %s:%d\n", host, port);

    if (sodium_init() < 0) {
        printf("ERROR: Failed to initialize libsodium\n");
        return 1;
    }

    uint8_t psk[32];
    crypto_hash_sha256(psk, (uint8_t*)psk_hex, strlen(psk_hex));

    // Variable declarations
    uint8_t buffer[256];
    uint32_t message_type;
    int len;
    noise_handshake_t handshake;
    uint8_t client_hello[32];
    size_t client_hello_len;
    uint8_t framed_client_hello[35];
    uint8_t framed_server_hello[256];
    ssize_t framed_len;
    uint8_t empty_auth[] = {0x01, 0x00, 0x00};
    uint8_t server_auth[3];
    uint8_t hash[32];
    uint8_t send_key[32], recv_key[32];
    crypto_generichash_blake2b_state hkdf_state;

    // Initial connection
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Failed to create socket\n");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        printf("Invalid address\n");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Failed to connect\n");
        close(sock);
        return 1;
    }

    printf("Connected successfully\n");

    // Send protocol identifier frame first (like the working C++ implementation)
    uint8_t protocol_frame[] = {0x01, 0x00, 0x00, 0x01};
    if (send(sock, protocol_frame, sizeof(protocol_frame), 0) != sizeof(protocol_frame)) {
        printf("Failed to send protocol frame\n");
        close(sock);
        return 1;
    }

    printf("Sent protocol frame\n");

    // Receive device info
    framed_len = recv(sock, framed_server_hello, sizeof(framed_server_hello), 0);
    if (framed_len <= 0) {
        printf("Failed to read device info\n");
        close(sock);
        return 1;
    }

    printf("Received device info (%zd bytes)\n", framed_len);

    if (framed_len < 3 || framed_server_hello[0] != 0x01) {
        printf("Invalid device info framing\n");
        close(sock);
        return 1;
    }

    uint16_t device_info_len = (framed_server_hello[1] << 8) | framed_server_hello[2];
    printf("Device info length: %u\n", device_info_len);
    printf("Device info data: ");
    for (int i = 0; i < device_info_len && i < 50; i++) {
        printf("%02x ", framed_server_hello[3 + i]);
    }
    printf("\n");

    // Now initialize handshake and send ephemeral key directly (no framing)
    if (noise_handshake_init(&handshake, psk) != 0) {
        printf("Failed to init handshake\n");
        close(sock);
        return 1;
    }

    // Send ephemeral key directly (like the working C++ implementation)
    client_hello_len = sizeof(client_hello);
    if (noise_create_client_hello(&handshake, client_hello, &client_hello_len) != 0) {
        printf("Failed to create client hello\n");
        close(sock);
        return 1;
    }

    printf("Sending ephemeral key (%zu bytes)\n", client_hello_len);
    if (send(sock, client_hello, client_hello_len, 0) != (ssize_t)client_hello_len) {
        printf("Failed to send ephemeral key\n");
        close(sock);
        return 1;
    }

    printf("Sent ephemeral key\n");

    // Receive server hello
    framed_len = recv(sock, framed_server_hello, sizeof(framed_server_hello), 0);
    if (framed_len <= 0) {
        printf("Failed to read server hello\n");
        close(sock);
        return 1;
    }

    printf("Received framed server hello (%zd bytes)\n", framed_len);

    if (framed_len < 3 || framed_server_hello[0] != 0x01) {
        printf("Invalid server hello framing\n");
        close(sock);
        return 1;
    }

    uint16_t server_hello_hs_len = (framed_server_hello[1] << 8) | framed_server_hello[2];
    printf("Server hello HS length: %u\n", server_hello_hs_len);
    printf("Server hello HS data: ");
    for (int i = 0; i < server_hello_hs_len; i++) {
        printf("%02x ", framed_server_hello[3 + i]);
    }
    printf("\n");
    if (framed_len != 3 + server_hello_hs_len) {
        printf("Server hello length mismatch\n");
        close(sock);
        return 1;
    }

    // Process server hello
    if (noise_process_server_hello(&handshake, &framed_server_hello[3], server_hello_hs_len) != 0) {
        printf("Failed to process server hello\n");
        close(sock);
        return 1;
    }

    printf("Processed server hello\n");

    // Send client auth (empty for NNpsk0)
    if (send(sock, empty_auth, 3, 0) != 3) {
        printf("Failed to send client auth\n");
        close(sock);
        return 1;
    }

    printf("Sent client auth\n");

    // Receive server auth (empty for NNpsk0)
    if (recv(sock, server_auth, 3, 0) != 3) {
        printf("Failed to receive server auth\n");
        close(sock);
        return 1;
    }

    if (server_auth[0] != 0x01 || server_auth[1] != 0x00 || server_auth[2] != 0x00) {
        printf("Invalid server auth\n");
        close(sock);
        return 1;
    }

    // Derive encryption keys using the same method as the protocol implementation
    // Send key
    crypto_generichash_blake2b_init(&hkdf_state, NULL, 0, 32);
    crypto_generichash_blake2b_update(&hkdf_state, handshake.psk, 32);
    crypto_generichash_blake2b_update(&hkdf_state, (const uint8_t*)"send", 4);
    crypto_generichash_blake2b_final(&hkdf_state, send_key, 32);

    // Recv key
    crypto_generichash_blake2b_init(&hkdf_state, NULL, 0, 32);
    crypto_generichash_blake2b_update(&hkdf_state, handshake.psk, 32);
    crypto_generichash_blake2b_update(&hkdf_state, (const uint8_t*)"recv", 4);
    crypto_generichash_blake2b_final(&hkdf_state, recv_key, 32);

    // Initialize cipher states
    memcpy(handshake.send_cipher_state.key, send_key, 32);
    handshake.send_cipher_state.nonce = 0;
    handshake.send_cipher_state.initialized = true;

    memcpy(handshake.recv_cipher_state.key, recv_key, 32);
    handshake.recv_cipher_state.nonce = 0;
    handshake.recv_cipher_state.initialized = true;

    handshake.state = NOISE_STATE_HANDSHAKE;

    // Handshake complete

    // Set encryption keys
    esphome_set_encryption_keys(send_key, recv_key);

    printf("Encryption keys set\n");

    // Send encrypted hello request
    if (esphome_send_hello(sock) != 0) {
        printf("Failed to send encrypted hello\n");
        close(sock);
        return 1;
    }

    printf("Sent encrypted hello\n");

    // Receive encrypted hello response
    len = esphome_read_message(sock, buffer, sizeof(buffer), &message_type);
    if (len < 0) {
        printf("Failed to read encrypted hello response\n");
        close(sock);
        return 1;
    }

    if (message_type != 2) {
        printf("Unexpected message type for hello response: %u\n", message_type);
        close(sock);
        return 1;
    }

    printf("Received encrypted hello response, len %d\n", len);

    // Send connect request (encrypted)
    if (esphome_send_connect(sock, "49trahreg48") != 0) {
        printf("Failed to send connect request\n");
        close(sock);
        return 1;
    }

    printf("Sent connect request\n");

    // Receive connect response (encrypted)
    len = esphome_read_message(sock, buffer, sizeof(buffer), &message_type);
    if (len < 0) {
        printf("Failed to read connect response\n");
        close(sock);
        return 1;
    }

    printf("Received connect response, type %u\n", message_type);

    // Send switch command to toggle relay (assume key 1)
    if (esphome_send_switch_command(sock, 1, true) != 0) {
        printf("Failed to send switch command\n");
        close(sock);
        return 1;
    }

    printf("Sent switch command to toggle relay\n");

    // Receive switch response (optional)
    len = esphome_read_message(sock, buffer, sizeof(buffer), &message_type);
    if (len >= 0) {
        printf("Received switch response, type %u\n", message_type);
    } else {
        printf("No switch response or failed to read\n");
    }

    // Success
    printf("Encrypted connection and relay toggle successful\n");

    close(sock);
    return 0;
}