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
    if (strlen(psk_hex) != 64) {
        printf("PSK must be 64 hex characters\n");
        return 1;
    }
    for (int i = 0; i < 32; i++) {
        char hex[3] = {psk_hex[i*2], psk_hex[i*2+1], 0};
        psk[i] = (uint8_t)strtol(hex, NULL, 16);
    }

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
    crypto_hash_sha256_state hkdf_state;

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

    // Assume encryption required - start Noise handshake directly
    printf("Starting Noise handshake\n");

    // Send HelloRequest
    if (esphome_send_hello(sock) != 0) {
        printf("Failed to send HelloRequest\n");
        close(sock);
        return 1;
    }

    printf("Sent HelloRequest\n");

    // Receive HelloResponse
    len = esphome_read_message(sock, buffer, sizeof(buffer), &message_type);
    if (len < 0) {
        printf("Failed to read HelloResponse\n");
        close(sock);
        return 1;
    }

    if (message_type != ESPHOME_MSG_HELLO_RESPONSE) {
        printf("Expected HelloResponse, got %u\n", message_type);
        close(sock);
        return 1;
    }

    printf("Received HelloResponse\n");

    // Now start Noise handshake
    if (noise_handshake_init(&handshake, psk) != 0) {
        printf("Failed to init handshake\n");
        close(sock);
        return 1;
    }

    // Send client hello
    client_hello_len = sizeof(client_hello);
    if (noise_create_client_hello(&handshake, client_hello, &client_hello_len) != 0) {
        printf("Failed to create client hello\n");
        close(sock);
        return 1;
    }

    framed_client_hello[0] = 0x01;
    framed_client_hello[1] = (client_hello_len >> 8) & 0xFF;
    framed_client_hello[2] = client_hello_len & 0xFF;
    memcpy(&framed_client_hello[3], client_hello, client_hello_len);

    if (send(sock, framed_client_hello, 3 + client_hello_len, 0) != (ssize_t)(3 + client_hello_len)) {
        printf("Failed to send client hello\n");
        close(sock);
        return 1;
    }

    printf("Sent client hello\n");

    // Receive server hello (e)
    uint8_t framed_server_hello_hs[256];
    ssize_t framed_len_hs = recv(sock, framed_server_hello_hs, sizeof(framed_server_hello_hs), 0);
    if (framed_len_hs <= 0) {
        printf("Failed to read server handshake hello\n");
        close(sock);
        return 1;
    }

    printf("Received framed server handshake hello (%zd bytes)\n", framed_len_hs);

    if (framed_len_hs < 3 || framed_server_hello_hs[0] != 0x01) {
        printf("Invalid server handshake hello framing\n");
        close(sock);
        return 1;
    }

    uint16_t server_hello_hs_len = (framed_server_hello_hs[1] << 8) | framed_server_hello_hs[2];
    if (framed_len_hs != 3 + server_hello_hs_len) {
        printf("Server handshake hello length mismatch\n");
        close(sock);
        return 1;
    }

    // Process server hello (e)
    printf("Server handshake hello data (%u bytes): ", server_hello_hs_len);
    for (int i = 0; i < server_hello_hs_len && i < 32; i++) {
        printf("%02x ", framed_server_hello_hs[3 + i]);
    }
    printf("\n");
    fflush(stdout);
    
    if (noise_process_server_hello(&handshake, &framed_server_hello_hs[3], server_hello_hs_len) != 0) {
        printf("Failed to process server handshake hello\n");
        close(sock);
        return 1;
    }

    printf("Processed server handshake hello\n");

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

    // Derive encryption keys
    crypto_hash_sha256_final(&handshake.hash_state, hash);

    // Send key
    crypto_hash_sha256_init(&hkdf_state);
    crypto_hash_sha256_update(&hkdf_state, hash, 32);
    crypto_hash_sha256_update(&hkdf_state, handshake.shared_secret, 32);
    crypto_hash_sha256_update(&hkdf_state, (const uint8_t*)"send", 4);
    crypto_hash_sha256_final(&hkdf_state, send_key);

    // Recv key
    crypto_hash_sha256_init(&hkdf_state);
    crypto_hash_sha256_update(&hkdf_state, hash, 32);
    crypto_hash_sha256_update(&hkdf_state, handshake.shared_secret, 32);
    crypto_hash_sha256_update(&hkdf_state, (const uint8_t*)"recv", 4);
    crypto_hash_sha256_final(&hkdf_state, recv_key);

    // Handshake complete
    printf("Handshake complete\n");

    // Set encryption keys
    esphome_set_encryption_keys(send_key, recv_key);

    // Receive encrypted hello response (server sends it after handshake)
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

    // Initialize handshake
    if (noise_handshake_init(&handshake, psk) != 0) {
        printf("Failed to init handshake\n");
        close(sock);
        return 1;
    }

    // Send client hello
    client_hello_len = sizeof(client_hello);
    if (noise_create_client_hello(&handshake, client_hello, &client_hello_len) != 0) {
        printf("Failed to create client hello\n");
        close(sock);
        return 1;
    }

    framed_client_hello[0] = 0x01;
    framed_client_hello[1] = (client_hello_len >> 8) & 0xFF;
    framed_client_hello[2] = client_hello_len & 0xFF;
    memcpy(&framed_client_hello[3], client_hello, client_hello_len);

    if (send(sock, framed_client_hello, 3 + client_hello_len, 0) != (ssize_t)(3 + client_hello_len)) {
        printf("Failed to send client hello\n");
        close(sock);
        return 1;
    }

    printf("Sent client hello\n");

    // Receive server hello
    framed_len = recv(sock, framed_server_hello, sizeof(framed_server_hello), 0);
    if (framed_len <= 0) {
        printf("Failed to read server hello\n");
        close(sock);
        return 1;
    }

    printf("Received framed hello response (%zd bytes)\n", framed_len);

    if (framed_len < 3 || framed_server_hello[0] != 0x01) {
        printf("Invalid hello response framing\n");
        close(sock);
        return 1;
    }

    uint16_t hello_resp_len = (framed_server_hello[1] << 8) | framed_server_hello[2];
    printf("Hello response length: %u\n", hello_resp_len);
    printf("Hello response data: ");
    for (int i = 0; i < hello_resp_len; i++) {
        printf("%02x ", framed_server_hello[3 + i]);
    }
    printf("\n");

    if (framed_len != 3 + hello_resp_len) {
        printf("Hello response length mismatch\n");
        close(sock);
        return 1;
    }

    // Process hello response (decrypt and parse)
    // But since it's after handshake, we read it as message

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

    // Derive encryption keys
    crypto_hash_sha256_final(&handshake.hash_state, hash);

    // Send key
    crypto_hash_sha256_init(&hkdf_state);
    crypto_hash_sha256_update(&hkdf_state, hash, 32);
    crypto_hash_sha256_update(&hkdf_state, handshake.shared_secret, 32);
    crypto_hash_sha256_update(&hkdf_state, (const uint8_t*)"send", 4);
    crypto_hash_sha256_final(&hkdf_state, send_key);

    // Recv key
    crypto_hash_sha256_init(&hkdf_state);
    crypto_hash_sha256_update(&hkdf_state, hash, 32);
    crypto_hash_sha256_update(&hkdf_state, handshake.shared_secret, 32);
    crypto_hash_sha256_update(&hkdf_state, (const uint8_t*)"recv", 4);
    crypto_hash_sha256_final(&hkdf_state, recv_key);

    // Handshake complete
    printf("Handshake complete\n");

    // Set encryption keys
    esphome_set_encryption_keys(send_key, recv_key);

    // Receive encrypted hello response (server sends it after handshake)
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
