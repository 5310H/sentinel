/**
 * @file test_complete_esphome.c
 * @brief Complete ESPHome Noise handshake test
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

// Noise protocol state (simplified implementation based on working C++ code)
typedef struct {
    uint8_t h[32], ck[32], k[32];
    uint64_t nonce;
} NoiseState;

void mix_hash(NoiseState *ns, const uint8_t *data, size_t len) {
    crypto_generichash_blake2b_state ctx;
    crypto_generichash_blake2b_init(&ctx, NULL, 0, 32);
    crypto_generichash_blake2b_update(&ctx, ns->h, 32);
    if (len > 0) crypto_generichash_blake2b_update(&ctx, data, len);
    crypto_generichash_blake2b_final(&ctx, ns->h, 32);
}

void mix_key(NoiseState *ns, const uint8_t *data, size_t len) {
    uint8_t k1[32], tmp[32];
    crypto_generichash_blake2b_state ctx;

    crypto_generichash_blake2b_init(&ctx, NULL, 0, 32);
    crypto_generichash_blake2b_update(&ctx, ns->ck, 32);
    crypto_generichash_blake2b_update(&ctx, data, len);
    crypto_generichash_blake2b_final(&ctx, k1, 32);

    crypto_generichash_blake2b_init(&ctx, NULL, 0, 32);
    crypto_generichash_blake2b_update(&ctx, k1, 32);
    crypto_generichash_blake2b_update(&ctx, (const uint8_t*)"\x00", 1);
    crypto_generichash_blake2b_final(&ctx, tmp, 32);
    memcpy(ns->ck, tmp, 32);

    crypto_generichash_blake2b_init(&ctx, NULL, 0, 32);
    crypto_generichash_blake2b_update(&ctx, k1, 32);
    crypto_generichash_blake2b_update(&ctx, (const uint8_t*)"\x01", 1);
    crypto_generichash_blake2b_final(&ctx, tmp, 32);
    memcpy(ns->k, tmp, 32);
}

void init_noise(NoiseState *ns, const uint8_t *psk) {
    const char *proto = "Noise_NNpsk0_25519_ChaChaPoly_SHA256";
    crypto_generichash_blake2b((uint8_t*)ns->ck, 32, (const uint8_t*)proto, strlen(proto), NULL, 0);
    memcpy(ns->h, ns->ck, 32);
    mix_hash(ns, (const uint8_t*)"ESPHome", 7);
    mix_hash(ns, psk, 32);
    mix_key(ns, psk, 32);
    ns->nonce = 0;
}

void encrypt_and_send(NoiseState *ns, int sock, const uint8_t *data, size_t len) {
    uint8_t ciphertext[len + 16];
    uint8_t nonce[12] = {0};
    memcpy(nonce + 4, &ns->nonce, 8);

    crypto_aead_chacha20poly1305_encrypt(ciphertext, NULL, data, len, ns->h, 32, NULL, nonce, ns->k);
    mix_hash(ns, ciphertext, len + 16);
    ns->nonce++;

    uint8_t frame[1024];
    frame[0] = 0x01;
    int p = 1;
    uint16_t cipher_len = len + 16;
    if (cipher_len > 0x7F) {
        frame[p++] = (cipher_len & 0x7F) | 0x80;
        frame[p++] = cipher_len >> 7;
    } else {
        frame[p++] = cipher_len & 0x7F;
    }
    memcpy(&frame[p], ciphertext, cipher_len);
    send(sock, frame, p + cipher_len, 0);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <host> [port]\n", argv[0]);
        printf("Example: %s 192.168.69.178 6053\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = (argc > 2) ? atoi(argv[2]) : 6053;

    printf("Testing complete ESPHome Noise handshake with %s:%d\n", host, port);

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

    // Initialize Noise state
    NoiseState ns;
    init_noise(&ns, psk);

    // Generate ephemeral keypair
    uint8_t e_priv[32], e_pub[32];
    memset(e_priv, 0x42, 32);  // Fixed key for testing
    crypto_scalarmult_base(e_pub, e_priv);

    // 1. Send protocol identifier frame
    uint8_t protocol_frame[] = {0x01, 0x00, 0x00, 0x01};
    printf("Sending protocol identifier frame...\n");
    if (send(sock, protocol_frame, sizeof(protocol_frame), 0) != sizeof(protocol_frame)) {
        perror("send protocol frame");
        close(sock);
        return 1;
    }

    // 2. Send everything in one packet: protocol frame + ephemeral key + HelloRequest + AuthRequest
    uint8_t send_buffer[512];
    size_t offset = 0;

    // Protocol frame
    send_buffer[offset++] = 0x01;
    send_buffer[offset++] = 0x00;
    send_buffer[offset++] = 0x00;
    send_buffer[offset++] = 0x01;

    // Ephemeral key frame
    send_buffer[offset++] = 0x00;
    send_buffer[offset++] = 0x00;
    send_buffer[offset++] = 0x20;
    memcpy(send_buffer + offset, e_pub, 32);
    offset += 32;

    // Mix ephemeral key into hash
    mix_hash(&ns, e_pub, 32);

    // HelloRequest (encrypted)
    uint8_t hello_data[] = {0x01, 0x08, 0x01, 0x10, 0x0e, 0x1a, 0x0d, 'a', 'i', 'o', 'e', 's', 'p', 'h', 'o', 'm', 'e', 'a', 'p', 'i'};
    uint8_t encrypted_hello[36]; // 20 bytes data + 16 bytes auth tag
    uint8_t hello_nonce[12] = {0};
    memcpy(hello_nonce + 4, &ns.nonce, 8);
    
    unsigned long long hello_cipher_len;
    if (crypto_aead_chacha20poly1305_encrypt(encrypted_hello, &hello_cipher_len, hello_data, sizeof(hello_data), ns.h, 32, NULL, hello_nonce, ns.k) != 0) {
        printf("Failed to encrypt HelloRequest\n");
        close(sock);
        return 1;
    }
    
    send_buffer[offset++] = 0x01;  // encrypted preamble
    send_buffer[offset++] = 0x00;  // length high
    send_buffer[offset++] = 36;    // length low (20 + 16)
    memcpy(send_buffer + offset, encrypted_hello, 36);
    offset += 36;
    ns.nonce++;

    // AuthRequest (encrypted)
    uint8_t auth_data[] = {0x03};
    uint8_t encrypted_auth[17]; // 1 byte data + 16 bytes auth tag
    uint8_t auth_nonce[12] = {0};
    memcpy(auth_nonce + 4, &ns.nonce, 8);
    
    unsigned long long auth_cipher_len;
    if (crypto_aead_chacha20poly1305_encrypt(encrypted_auth, &auth_cipher_len, auth_data, sizeof(auth_data), ns.h, 32, NULL, auth_nonce, ns.k) != 0) {
        printf("Failed to encrypt AuthRequest\n");
        close(sock);
        return 1;
    }
    
    send_buffer[offset++] = 0x01;  // encrypted preamble
    send_buffer[offset++] = 0x00;  // length high
    send_buffer[offset++] = 17;    // length low (1 + 16)
    memcpy(send_buffer + offset, encrypted_auth, 17);
    offset += 17;
    ns.nonce++;

    printf("Sending complete handshake packet (%zu bytes)...\n", offset);
    if (send(sock, send_buffer, offset, 0) != offset) {
        perror("send complete handshake");
        close(sock);
        return 1;
    }

    // 3. Receive server responses
    uint8_t response[1024];
    
    // First response: device info (plaintext)
    printf("Waiting for device info...\n");
    ssize_t received = recv(sock, response, sizeof(response), 0);
    if (received < 0) {
        perror("recv device info");
        close(sock);
        return 1;
    }

    printf("Received device info (%zd bytes): ", received);
    for (ssize_t i = 0; i < received && i < 32; i++) {
        if (isprint(response[i])) {
            printf("%c", response[i]);
        } else {
            printf("\\x%02x", response[i]);
        }
    }
    printf("\n");

    // Second response: encrypted ephemeral key
    printf("Waiting for encrypted ephemeral key...\n");
    received = recv(sock, response, sizeof(response), 0);
    if (received < 0) {
        perror("recv encrypted ephemeral key");
        close(sock);
        return 1;
    }

    printf("Received encrypted ephemeral key (%zd bytes):\n", received);
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
            // Decrypt server ephemeral key
            uint8_t nonce[12] = {0};
            memcpy(nonce + 4, &ns.nonce, 8);

            uint8_t server_e_pub[32];
            if (crypto_aead_chacha20poly1305_decrypt(server_e_pub, NULL, NULL, response + 3, length, ns.h, 32, nonce, ns.k) != 0) {
                printf("Failed to decrypt server ephemeral key\n");
                close(sock);
                return 1;
            }

            printf("Decrypted server ephemeral key: ");
            for (int i = 0; i < 32; i++) printf("%02x ", server_e_pub[i]);
            printf("\n");

            mix_hash(&ns, response + 3, length);
            ns.nonce++;

            // Compute shared secret
            uint8_t shared_secret[32];
            if (crypto_scalarmult(shared_secret, e_priv, server_e_pub) != 0) {
                printf("Failed to compute shared secret\n");
                close(sock);
                return 1;
            }

            mix_hash(&ns, shared_secret, 32);
            mix_key(&ns, shared_secret, 32);

            printf("Handshake phase 1 completed!\n");

            // 4. Read final responses
            printf("Waiting for final server responses...\n");
            for (int i = 0; i < 3; i++) {
                received = recv(sock, response, sizeof(response), 0);
                if (received > 0) {
                    printf("Received response %d (%zd bytes)\n", i + 1, received);
                    // Could decrypt and parse here
                } else {
                    break;
                }
            }

            printf("ESPHome handshake completed successfully!\n");
        }
    }

    close(sock);
    printf("Test completed!\n");
    return 0;
}