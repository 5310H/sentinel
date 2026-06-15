/**
 * @file test_esphome_client.c
 * @brief Complete ESPHome client implementation using noise-c library
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

// Include noise-c library
#include <noise/protocol.h>

// ESPHome protocol constants
#define ESPHOME_MSG_HELLO_REQUEST 1
#define ESPHOME_MSG_HELLO_RESPONSE 2
#define ESPHOME_MSG_CONNECT_REQUEST 3
#define ESPHOME_MSG_CONNECT_RESPONSE 4
#define ESPHOME_MSG_DISCONNECT_REQUEST 5
#define ESPHOME_MSG_DISCONNECT_RESPONSE 6
#define ESPHOME_MSG_PING_REQUEST 7
#define ESPHOME_MSG_PING_RESPONSE 8
#define ESPHOME_MSG_DEVICE_INFO_REQUEST 9
#define ESPHOME_MSG_DEVICE_INFO_RESPONSE 10
#define ESPHOME_MSG_LIST_ENTITIES_REQUEST 11
#define ESPHOME_MSG_LIST_ENTITIES_RESPONSE 12
#define ESPHOME_MSG_SUBSCRIBE_STATES_REQUEST 13
#define ESPHOME_MSG_SUBSCRIBE_STATES_RESPONSE 14
#define ESPHOME_MSG_ENTITY_UPDATE 15
#define ESPHOME_MSG_SWITCH_COMMAND_REQUEST 16
#define ESPHOME_MSG_SWITCH_COMMAND_RESPONSE 17
#define ESPHOME_MSG_NOISE_HANDSHAKE 18

// Message framing
#define PREAMBLE_PLAIN 0x00
#define PREAMBLE_ENCRYPTED 0x01

typedef struct {
    int sock;
    NoiseHandshakeState *handshake;
    NoiseCipherState *send_cipher;
    NoiseCipherState *recv_cipher;
    int handshake_complete;
} esphome_client_t;

int esphome_connect(esphome_client_t *client, const char *host, int port) {
    struct sockaddr_in server_addr;

    // Create socket
    client->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client->sock < 0) {
        perror("socket");
        return -1;
    }

    // Set up server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(client->sock);
        return -1;
    }

    // Connect
    if (connect(client->sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(client->sock);
        return -1;
    }

    return 0;
}

int esphome_send_frame(esphome_client_t *client, uint8_t preamble, const uint8_t *data, size_t data_len) {
    uint8_t frame[1024];
    size_t frame_len = 3 + data_len;

    if (frame_len > sizeof(frame)) {
        return -1;
    }

    frame[0] = preamble;
    frame[1] = (data_len >> 8) & 0xFF;
    frame[2] = data_len & 0xFF;
    memcpy(frame + 3, data, data_len);

    if (send(client->sock, frame, frame_len, 0) != (ssize_t)frame_len) {
        perror("send");
        return -1;
    }

    return 0;
}

int esphome_recv_frame(esphome_client_t *client, uint8_t *preamble, uint8_t *buffer, size_t buffer_size, size_t *data_len) {
    uint8_t header[3];

    if (recv(client->sock, header, 3, MSG_WAITALL) != 3) {
        perror("recv header");
        return -1;
    }

    *preamble = header[0];
    *data_len = (header[1] << 8) | header[2];

    if (*data_len > buffer_size) {
        fprintf(stderr, "Frame too large: %zu > %zu\n", *data_len, buffer_size);
        return -1;
    }

    if (recv(client->sock, buffer, *data_len, MSG_WAITALL) != (ssize_t)*data_len) {
        perror("recv data");
        return -1;
    }

    return 0;
}

int esphome_send_encrypted(esphome_client_t *client, const uint8_t *data, size_t data_len) {
    if (!client->handshake_complete) {
        return -1;
    }

    uint8_t encrypted[1024];
    NoiseBuffer mbuf;

    noise_buffer_set_inout(mbuf, encrypted, sizeof(encrypted), data_len);
    memcpy(mbuf.data, data, data_len);

    int err = noise_cipherstate_encrypt(client->send_cipher, &mbuf);
    if (err != NOISE_ERROR_NONE) {
        noise_perror("encrypt", err);
        return -1;
    }

    return esphome_send_frame(client, PREAMBLE_ENCRYPTED, encrypted, mbuf.size);
}

int esphome_recv_encrypted(esphome_client_t *client, uint8_t *buffer, size_t buffer_size, size_t *data_len) {
    if (!client->handshake_complete) {
        return -1;
    }

    uint8_t preamble;
    uint8_t encrypted[1024];
    size_t encrypted_len;

    int ret = esphome_recv_frame(client, &preamble, encrypted, sizeof(encrypted), &encrypted_len);
    if (ret != 0) {
        return ret;
    }

    if (preamble != PREAMBLE_ENCRYPTED) {
        fprintf(stderr, "Expected encrypted frame, got preamble 0x%02x\n", preamble);
        return -1;
    }

    NoiseBuffer mbuf;
    noise_buffer_set_input(mbuf, encrypted, encrypted_len);

    int err = noise_cipherstate_decrypt(client->recv_cipher, &mbuf);
    if (err != NOISE_ERROR_NONE) {
        noise_perror("decrypt", err);
        return -1;
    }

    if (mbuf.size > buffer_size) {
        return -1;
    }

    memcpy(buffer, mbuf.data, mbuf.size);
    *data_len = mbuf.size;

    return 0;
}

int esphome_noise_handshake(esphome_client_t *client, const uint8_t *psk) {
    NoiseBuffer mbuf;
    uint8_t buffer[1024];
    int err;
    int action;

    // Initialize noise library
    if (noise_init() != NOISE_ERROR_NONE) {
        fprintf(stderr, "Failed to initialize noise library\n");
        return -1;
    }

    // Create handshake state
    err = noise_handshakestate_new_by_name(&client->handshake, "Noise_NNpsk0_25519_ChaChaPoly_SHA256", NOISE_ROLE_INITIATOR);
    if (err != NOISE_ERROR_NONE) {
        noise_perror("create handshake", err);
        return -1;
    }

    // Set prologue
    const char *prologue = "NoiseAPIInit\x00\x00";
    err = noise_handshakestate_set_prologue(client->handshake, (const uint8_t *)prologue, strlen(prologue));
    if (err != NOISE_ERROR_NONE) {
        noise_perror("set prologue", err);
        goto cleanup;
    }

    // Set PSK
    err = noise_handshakestate_set_pre_shared_key(client->handshake, psk, 32);
    if (err != NOISE_ERROR_NONE) {
        noise_perror("set PSK", err);
        goto cleanup;
    }

    // Start handshake
    err = noise_handshakestate_start(client->handshake);
    if (err != NOISE_ERROR_NONE) {
        noise_perror("start handshake", err);
        goto cleanup;
    }

    // Run handshake loop
    while (1) {
        action = noise_handshakestate_get_action(client->handshake);
        if (action == NOISE_ACTION_WRITE_MESSAGE) {
            // Send handshake message
            noise_buffer_set_output(mbuf, buffer + 3, sizeof(buffer) - 3);
            err = noise_handshakestate_write_message(client->handshake, &mbuf, NULL);
            if (err != NOISE_ERROR_NONE) {
                noise_perror("write handshake", err);
                goto cleanup;
            }

            buffer[0] = PREAMBLE_ENCRYPTED;
            buffer[1] = (mbuf.size >> 8) & 0xFF;
            buffer[2] = mbuf.size & 0xFF;

            if (send(client->sock, buffer, mbuf.size + 3, 0) != (ssize_t)(mbuf.size + 3)) {
                perror("send handshake");
                goto cleanup;
            }

        } else if (action == NOISE_ACTION_READ_MESSAGE) {
            // Receive handshake message
            uint8_t preamble;
            size_t data_len;

            if (esphome_recv_frame(client, &preamble, buffer, sizeof(buffer), &data_len) != 0) {
                goto cleanup;
            }

            if (preamble != PREAMBLE_ENCRYPTED) {
                fprintf(stderr, "Expected encrypted handshake message\n");
                goto cleanup;
            }

            noise_buffer_set_input(mbuf, buffer, data_len);
            err = noise_handshakestate_read_message(client->handshake, &mbuf, NULL);
            if (err != NOISE_ERROR_NONE) {
                noise_perror("read handshake", err);
                goto cleanup;
            }

        } else if (action == NOISE_ACTION_SPLIT) {
            // Handshake complete
            err = noise_handshakestate_split(client->handshake, &client->send_cipher, &client->recv_cipher);
            if (err != NOISE_ERROR_NONE) {
                noise_perror("split handshake", err);
                goto cleanup;
            }

            client->handshake_complete = 1;
            noise_handshakestate_free(client->handshake);
            client->handshake = NULL;
            return 0;

        } else {
            // Failed
            fprintf(stderr, "Handshake failed\n");
            goto cleanup;
        }
    }

cleanup:
    if (client->handshake) {
        noise_handshakestate_free(client->handshake);
        client->handshake = NULL;
    }
    return -1;
}

int esphome_hello(esphome_client_t *client) {
    // Send empty hello
    uint8_t empty[0];
    if (esphome_send_frame(client, PREAMBLE_PLAIN, empty, 0) != 0) {
        return -1;
    }

    // Receive device info
    uint8_t preamble;
    uint8_t buffer[1024];
    size_t data_len;

    if (esphome_recv_frame(client, &preamble, buffer, sizeof(buffer), &data_len) != 0) {
        return -1;
    }

    if (preamble != PREAMBLE_ENCRYPTED) {
        fprintf(stderr, "Expected encrypted device info\n");
        return -1;
    }

    printf("Received device info (%zu bytes): ", data_len);
    for (size_t i = 0; i < data_len && i < 50; i++) {
        if (buffer[i] >= 32 && buffer[i] < 127) {
            printf("%c", buffer[i]);
        } else {
            printf("\\x%02x", buffer[i]);
        }
    }
    printf("\n");

    return 0;
}

int esphome_connect_request(esphome_client_t *client, const char *password) {
    // Send connect request
    uint8_t buffer[256];
    size_t len = strlen(password);
    buffer[0] = ESPHOME_MSG_CONNECT_REQUEST;
    memcpy(buffer + 1, password, len);

    if (esphome_send_encrypted(client, buffer, len + 1) != 0) {
        return -1;
    }

    // Receive connect response
    size_t resp_len;
    if (esphome_recv_encrypted(client, buffer, sizeof(buffer), &resp_len) != 0) {
        return -1;
    }

    if (resp_len < 1 || buffer[0] != ESPHOME_MSG_CONNECT_RESPONSE) {
        fprintf(stderr, "Invalid connect response\n");
        return -1;
    }

    printf("Connected successfully\n");
    return 0;
}

void esphome_disconnect(esphome_client_t *client) {
    if (client->sock >= 0) {
        close(client->sock);
        client->sock = -1;
    }

    if (client->send_cipher) {
        noise_cipherstate_free(client->send_cipher);
        client->send_cipher = NULL;
    }

    if (client->recv_cipher) {
        noise_cipherstate_free(client->recv_cipher);
        client->recv_cipher = NULL;
    }

    if (client->handshake) {
        noise_handshakestate_free(client->handshake);
        client->handshake = NULL;
    }

    client->handshake_complete = 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <host> [psk_base64]\n", argv[0]);
        printf("Example: %s 192.168.69.179 93lQ8xeyF152/qDFEOngsSIJ74BrNRIOvXPVs8yNOIQ=\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    const char *psk_base64 = (argc > 2) ? argv[2] : "93lQ8xeyF152/qDFEOngsSIJ74BrNRIOvXPVs8yNOIQ=";

    printf("Testing ESPHome client against %s\n", host);

    // Initialize libsodium
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }

    // Decode PSK
    uint8_t psk[32];
    size_t decoded_len;
    if (sodium_base642bin(psk, sizeof(psk), psk_base64, strlen(psk_base64),
                         NULL, &decoded_len, NULL, sodium_base64_VARIANT_ORIGINAL) != 0 ||
        decoded_len != 32) {
        fprintf(stderr, "Invalid PSK\n");
        return 1;
    }

    // Initialize client
    esphome_client_t client = {0};
    client.sock = -1;

    // Connect
    if (esphome_connect(&client, host, 6053) != 0) {
        fprintf(stderr, "Failed to connect\n");
        return 1;
    }

    printf("Connected to %s\n", host);

    // Send hello and receive device info
    if (esphome_hello(&client) != 0) {
        fprintf(stderr, "Hello failed\n");
        esphome_disconnect(&client);
        return 1;
    }

    // Perform Noise handshake
    if (esphome_noise_handshake(&client, psk) != 0) {
        fprintf(stderr, "Noise handshake failed\n");
        esphome_disconnect(&client);
        return 1;
    }

    printf("Noise handshake completed successfully!\n");

    // Send connect request
    if (esphome_connect_request(&client, "49trahreg48") != 0) {
        fprintf(stderr, "Connect request failed\n");
        esphome_disconnect(&client);
        return 1;
    }

    // Success - keep connection open for a moment
    sleep(2);

    esphome_disconnect(&client);
    printf("Test completed successfully\n");
    return 0;
}