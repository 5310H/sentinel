/**
 * @file esphome_api_protocol.h
 * @brief ESPHome Native API binary protocol definitions
 * 
 * Protocol specification: https://esphome.io/api/native-api.html
 */

#ifndef ESPHOME_API_PROTOCOL_H
#define ESPHOME_API_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sodium/crypto_generichash_blake2b.h>

// Custom Noise protocol structures (no external library dependency)

// ESPHome API message types
#define ESPHOME_MSG_HELLO_REQUEST                  1
#define ESPHOME_MSG_HELLO_RESPONSE                 2
#define ESPHOME_MSG_CONNECT_REQUEST                3
#define ESPHOME_MSG_CONNECT_RESPONSE               4
#define ESPHOME_MSG_DISCONNECT_REQUEST             5
#define ESPHOME_MSG_DISCONNECT_RESPONSE            6
#define ESPHOME_MSG_PING_REQUEST                   7
#define ESPHOME_MSG_PING_RESPONSE                  8
#define ESPHOME_MSG_DEVICE_INFO_REQUEST            9
#define ESPHOME_MSG_DEVICE_INFO_RESPONSE          10
#define ESPHOME_MSG_LIST_ENTITIES_REQUEST         11
#define ESPHOME_MSG_LIST_ENTITIES_BINARY_SENSOR   19
#define ESPHOME_MSG_LIST_ENTITIES_SWITCH          23
#define ESPHOME_MSG_LIST_ENTITIES_LIGHT           27
#define ESPHOME_MSG_LIST_ENTITIES_DONE            18
#define ESPHOME_MSG_SUBSCRIBE_STATES_REQUEST      20
#define ESPHOME_MSG_BINARY_SENSOR_STATE_RESPONSE  21
#define ESPHOME_MSG_SWITCH_STATE_RESPONSE         24
#define ESPHOME_MSG_LIGHT_STATE_RESPONSE          28
#define ESPHOME_MSG_SWITCH_COMMAND_REQUEST        30
#define ESPHOME_MSG_LIGHT_COMMAND_REQUEST         31

// Noise protocol message types
#define ESPHOME_MSG_NOISE_HELLO                   32
#define ESPHOME_MSG_NOISE_HANDSHAKE               33

// Varint encoding/decoding functions
int _encode_varint(uint8_t *buffer, uint32_t value);
int _decode_varint(const uint8_t *buffer, uint32_t *value);

// Message parsing functions
int esphome_read_message(int socket_fd, uint8_t *buffer, size_t buffer_size, uint32_t *message_type);

// Hello Request (message type 1)
typedef struct {
    char client_info[64];
    uint32_t api_version_major;
    uint32_t api_version_minor;
} esphome_hello_request_t;

// Hello Response (message type 2)
typedef struct {
    uint32_t api_version_major;
    uint32_t api_version_minor;
    char server_info[64];
    char name[64];
} esphome_hello_response_t;

// Connect Request (message type 3)
typedef struct {
    char password[64];
} esphome_connect_request_t;

// Connect Response (message type 4)
typedef struct {
    bool invalid_password;
} esphome_connect_response_t;

// Device Info Response (message type 10)
typedef struct {
    bool uses_password;
    char name[64];
    char mac_address[18];
    char esphome_version[16];
    char compilation_time[64];
    char model[64];
} esphome_device_info_t;

// Binary Sensor Entity (message type 19)
typedef struct {
    uint32_t key;
    char object_id[64];
    char name[64];
    char unique_id[64];
    char device_class[32];
    bool is_status_binary_sensor;
} esphome_binary_sensor_entity_t;

// Switch Entity (message type 23)
typedef struct {
    uint32_t key;
    char object_id[64];
    char name[64];
    char unique_id[64];
    char icon[32];
    bool assumed_state;
} esphome_switch_entity_t;

// Binary Sensor State (message type 21)
typedef struct {
    uint32_t key;
    bool state;
    bool missing_state;
} esphome_binary_sensor_state_t;

// Switch State (message type 24)
typedef struct {
    uint32_t key;
    bool state;
} esphome_switch_state_t;

// Switch Command (message type 30)
typedef struct {
    uint32_t key;
    bool state;
} esphome_switch_command_t;

// Noise protocol structures
typedef struct {
    uint8_t key[32];
    uint64_t nonce;
    bool initialized;
} noise_cipher_t;

typedef enum {
    NOISE_STATE_INIT,
    NOISE_STATE_HELLO,
    NOISE_STATE_HANDSHAKE,
    NOISE_STATE_READY,
    NOISE_STATE_CLOSED,
    NOISE_STATE_CLIENT_HELLO_RECEIVED,
    NOISE_STATE_SERVER_HELLO_SENT,
    NOISE_STATE_SERVER_HELLO_RECEIVED,
    NOISE_STATE_CLIENT_AUTH_SENT
} noise_state_t;

typedef struct {
    noise_state_t state;
    // For custom implementation
    uint8_t prologue[256];
    size_t prologue_len;
    uint8_t psk[32];
    // DH key pairs
    uint8_t e_private[32];
    uint8_t e_public[32];
    uint8_t re_public[32];
    uint8_t shared_secret[32];
    // Custom ciphers
    noise_cipher_t send_cipher_state;
    noise_cipher_t recv_cipher_state;
    // Hash input buffer for handshake
    uint8_t hash_input[1024];
    size_t hash_input_len;
    // Client hello data for prologue
    uint8_t client_hello_data[256];
    size_t client_hello_len;
} noise_handshake_t;

// Function prototypes
int esphome_encode_message(uint8_t *buffer, size_t buffer_len, uint32_t message_type, const void *data);
int esphome_decode_message(const uint8_t *buffer, size_t buffer_len, uint32_t *message_type, void *data);
int esphome_read_message(int socket_fd, uint8_t *buffer, size_t buffer_size, uint32_t *message_type);
int esphome_send_hello(int socket_fd);
int esphome_send_hello_with_noise(int socket_fd, const uint8_t *data, size_t data_len);
int esphome_send_connect(int socket_fd, const char *password);
int esphome_send_ping(int socket_fd);
int esphome_send_device_info_request(int socket_fd);
int esphome_send_list_entities_request(int socket_fd);
int esphome_send_subscribe_states_request(int socket_fd);
int esphome_send_switch_command(int socket_fd, uint32_t key, bool state);
int esphome_encode_switch_command(uint8_t *buffer, size_t buffer_size, uint32_t key, bool state);

// Noise protocol functions
int noise_handshake_init(noise_handshake_t *handshake, const uint8_t *psk);
int noise_handshake_start(noise_handshake_t *handshake, const uint8_t *client_hello, size_t client_hello_len);
int noise_create_client_hello(noise_handshake_t *handshake, uint8_t *buffer, size_t *buffer_len);
int noise_create_client_auth(noise_handshake_t *handshake, uint8_t *buffer, size_t *buffer_len);
int noise_process_server_hello(noise_handshake_t *handshake, const uint8_t *data, size_t data_len);
int noise_create_server_hello(noise_handshake_t *handshake, const uint8_t *psk, uint8_t *output, size_t *output_len);
int noise_process_client_hello(noise_handshake_t *handshake, const uint8_t *data, size_t data_len);
int noise_process_client_auth(noise_handshake_t *handshake, const uint8_t *psk, const uint8_t *data, size_t data_len);
int noise_handshake_free(noise_handshake_t *handshake);
int esphome_send_encrypted_message(int socket_fd, noise_handshake_t *handshake,
                                  uint32_t message_type, const uint8_t *data, size_t data_len);
int esphome_read_encrypted_message(int socket_fd, noise_handshake_t *handshake,
                                  uint8_t *buffer, size_t buffer_size, uint32_t *message_type);

extern void esphome_set_encryption_keys(const uint8_t *send_key, const uint8_t *recv_key);

#endif // ESPHOME_API_PROTOCOL_H
