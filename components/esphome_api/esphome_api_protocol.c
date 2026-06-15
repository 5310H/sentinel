/**
 * @file esphome_api_protocol.c
 * @brief ESPHome Native API protocol implementation
 * 
 * Implements ESPHome's protobuf-based binary protocol.
 * Messages are encoded as: [0x00][length][type][data]
 */

#include "esphome_api_protocol.h"
#include <string.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sodium.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "api.pb.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "ESPHOME_PROTO";
#else
#include <stdio.h>
#define TAG "ESPHOME_PROTO"
#endif

// Debug marker
#pragma message("Compiling esphome_api_protocol.c with debug markers")

// Encryption support
int esphome_encryption_enabled = 0;
uint8_t encryption_send_key[32];
uint8_t encryption_recv_key[32];
uint64_t send_nonce = 0;
uint64_t recv_nonce = 0;

void esphome_set_encryption_keys(const uint8_t *send_key, const uint8_t *recv_key) {
    memcpy(encryption_send_key, send_key, 32);
    memcpy(encryption_recv_key, recv_key, 32);
    esphome_encryption_enabled = 1;
    send_nonce = 0;
    recv_nonce = 0;
}

// Noise protocol constants
#define NOISE_HELLO ((const uint8_t*)"\x01\x00\x00")
#define NOISE_PROLOGUE "NoiseAPIInit\x00\x00"
#define NOISE_PROTOCOL_NAME "Noise_NNpsk0_25519_ChaChaPoly_SHA256"

/**
 * @brief Encode varint (Protocol Buffers variable-length integer)
 */
int _encode_varint(uint8_t *buffer, uint32_t value) {
    int len = 0;
    while (value > 0x7F) {
        buffer[len++] = (value & 0x7F) | 0x80;
        value >>= 7;
    }
    buffer[len++] = value & 0x7F;
    return len;
}

/**
 * @brief Decode varint
 */
int _decode_varint(const uint8_t *buffer, uint32_t *value) {
    *value = 0;
    int shift = 0;
    int len = 0;
    
    while (1) {
        uint8_t byte = buffer[len++];
        *value |= (byte & 0x7F) << shift;
        
        if ((byte & 0x80) == 0) {
            break;
        }
        
        shift += 7;
    }
    
    return len;
}

/**
 * @brief Read and parse ESPHome protocol message
 */
int esphome_read_message(int socket_fd, uint8_t *buffer, size_t buffer_size, uint32_t *message_type) {
    // Read preamble (0x00)
    uint8_t preamble;
    if (recv(socket_fd, &preamble, 1, 0) != 1) {
        return -1;
    }
    
    // Check preamble
    if (preamble == 0x01) {
        // Encryption required
        return -2;
    } else if (preamble != 0x00) {
        // Invalid preamble
        return -1;
    }
    
    // Read length (varint)
    uint32_t length;
    int len_bytes = 0;
    uint8_t len_buf[5];
    while (len_bytes < 5) {
        if (recv(socket_fd, &len_buf[len_bytes], 1, 0) != 1) {
            return -1;
        }
        if ((len_buf[len_bytes] & 0x80) == 0) {
            break;
        }
        len_bytes++;
    }
    _decode_varint(len_buf, &length);
    
    if (length > buffer_size) {
        return -1; // Message too large
    }
    
    // Read message type (varint)
    int type_bytes = 0;
    uint8_t type_buf[5];
    while (type_bytes < 5) {
        if (recv(socket_fd, &type_buf[type_bytes], 1, 0) != 1) {
            return -1;
        }
        if ((type_buf[type_bytes] & 0x80) == 0) {
            break;
        }
        type_bytes++;
    }
    _decode_varint(type_buf, message_type);
    
    // Read payload
    int payload_len = length - (type_bytes + 1);
    if (payload_len > 0) {
        if (recv(socket_fd, buffer, payload_len, 0) != payload_len) {
            return -1;
        }
    }
    
    return payload_len;
}

/**
 * @brief Send Hello request
 */
int esphome_send_hello(int socket_fd) {
    uint8_t buffer[128];
    int pos = 0;
    
    // Preamble
    buffer[pos++] = 0x00;
    
    // Message type first to calculate its length
    int msg_type_pos = pos;
    pos += _encode_varint(&buffer[pos], ESPHOME_MSG_HELLO_REQUEST);
    int msg_type_len = pos - msg_type_pos;
    
    // Length (placeholder) - insert after preamble
    uint8_t length_buf[5];
    int length_len = _encode_varint(length_buf, 0);  // Placeholder
    
    // Move message type to after length
    memmove(&buffer[1 + length_len], &buffer[1], msg_type_len);
    pos = 1 + length_len + msg_type_len;
    
    // Client info (field 1, string)
    buffer[pos++] = 0x0A;  // Field 1, wire type 2 (length-delimited)
    const char *client_info = "aioesphomeapi";
    int client_info_len = strlen(client_info);
    pos += _encode_varint(&buffer[pos], client_info_len);
    memcpy(&buffer[pos], client_info, client_info_len);
    pos += client_info_len;
    
    // API version major (field 2, varint)
    buffer[pos++] = 0x10;  // Field 2, wire type 0 (varint)
    pos += _encode_varint(&buffer[pos], 1);
    
    // API version minor (field 3, varint)
    buffer[pos++] = 0x18;  // Field 3, wire type 0 (varint)
    pos += _encode_varint(&buffer[pos], 9);
    
    // Calculate data length (everything after message type)
    uint32_t data_len = pos - (1 + length_len + msg_type_len);
    
    // Encode length at the correct position
    length_len = _encode_varint(&buffer[1], data_len);
    
    // Send message
    int sent = send(socket_fd, buffer, pos, 0);
    if (sent < 0) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Failed to send Hello request");
#else
        printf("[%s] ERROR: Failed to send Hello request\n", TAG);
#endif
        return -1;
    }
    
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Sent Hello request");
#else
    printf("[%s] Sent Hello request\n", TAG);
#endif
    
    return 0;
}

/**
 * @brief Send Connect request
 */
int esphome_send_connect(int socket_fd, const char *password) {
    uint8_t buffer[128];
    int pos = 0;
    
    // Preamble
    buffer[pos++] = 0x00;
    
    // Message type first to calculate its length
    int msg_type_pos = pos;
    pos += _encode_varint(&buffer[pos], ESPHOME_MSG_CONNECT_REQUEST);
    int msg_type_len = pos - msg_type_pos;
    
    // Length (placeholder) - insert after preamble
    uint8_t length_buf[5];
    int length_len = _encode_varint(length_buf, 0);  // Placeholder
    
    // Move message type to after length
    memmove(&buffer[1 + length_len], &buffer[1], msg_type_len);
    pos = 1 + length_len + msg_type_len;
    
    // Password (field 1, string)
    if (password && strlen(password) > 0) {
        buffer[pos++] = 0x0A;
        int password_len = strlen(password);
        pos += _encode_varint(&buffer[pos], password_len);
        memcpy(&buffer[pos], password, password_len);
        pos += password_len;
    }
    
    // Calculate data length (everything after message type)
    uint32_t data_len = pos - (1 + length_len + msg_type_len);
    
    // Encode length at the correct position
    length_len = _encode_varint(&buffer[1], data_len);
    
    send(socket_fd, buffer, pos, 0);
    
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Sent Connect request");
#else
    printf("[%s] Sent Connect request\n", TAG);
#endif
    
    return 0;
}

/**
 * @brief Send Ping request
 */
int esphome_send_ping(int socket_fd) {
    uint8_t buffer[16];
    int pos = 0;
    
    buffer[pos++] = 0x00;  // Preamble
    buffer[pos++] = 0x00;  // Length = 0 (no data)
    pos += _encode_varint(&buffer[pos], ESPHOME_MSG_PING_REQUEST);
    
    send(socket_fd, buffer, pos, 0);
    
    return 0;
}

/**
 * @brief Send Device Info Request
 */
int esphome_send_device_info_request(int socket_fd) {
    uint8_t buffer[16];
    int pos = 0;
    
    buffer[pos++] = 0x00;  // Preamble
    buffer[pos++] = 0x00;  // Length = 0
    pos += _encode_varint(&buffer[pos], ESPHOME_MSG_DEVICE_INFO_REQUEST);
    
    send(socket_fd, buffer, pos, 0);
    
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Sent Hello request");
#else
    printf("[%s] Sent Hello request\n", TAG);
#endif
    
    return 0;
}

/**
 * @brief Send List Entities Request
 */
int esphome_send_list_entities_request(int socket_fd) {
    uint8_t buffer[16];
    int pos = 0;
    
    buffer[pos++] = 0x00;  // Preamble
    buffer[pos++] = 0x00;  // Length = 0
    pos += _encode_varint(&buffer[pos], ESPHOME_MSG_LIST_ENTITIES_REQUEST);
    
    send(socket_fd, buffer, pos, 0);
    
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Sent List Entities Request");
#else
    printf("[%s] Sent List Entities Request\n", TAG);
#endif
    
    return 0;
}

/**
 * @brief Send Subscribe States Request
 */
int esphome_send_subscribe_states_request(int socket_fd) {
    uint8_t buffer[16];
    int pos = 0;
    
    buffer[pos++] = 0x00;  // Preamble
    buffer[pos++] = 0x00;  // Length = 0
    pos += _encode_varint(&buffer[pos], ESPHOME_MSG_SUBSCRIBE_STATES_REQUEST);
    
    send(socket_fd, buffer, pos, 0);
    
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Sent Subscribe States Request");
#else
    printf("[%s] Sent Subscribe States Request\n", TAG);
#endif
    
    return 0;
}

/**
 * @brief Send Switch Command
 */
int esphome_send_switch_command(int socket_fd, uint32_t key, bool state) {
    uint8_t buffer[128];
    int encoded_len = esphome_encode_switch_command(buffer, sizeof(buffer), key, state);
    if (encoded_len < 0) {
        return -1;
    }
    
    if (send(socket_fd, buffer, encoded_len, 0) != encoded_len) {
        return -1;
    }
    
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Sent Switch Command: key=%u, state=%d", key, state);
#else
    printf("[%s] Sent Switch Command: key=%" PRIu32 ", state=%d\n", TAG, key, state);
#endif
    
    return 0;
}

/**
 * @brief Encode Switch Command to buffer
 */
int esphome_encode_switch_command(uint8_t *buffer, size_t buffer_size, uint32_t key, bool state) {
    int pos = 0;
    
    if (buffer_size < 128) {
        return -1; // Buffer too small
    }
    
    buffer[pos++] = 0x00;  // Preamble
    
    // Create SwitchCommandRequest message
    SwitchCommandRequest switch_req = SwitchCommandRequest_init_zero;
    switch_req.key = key;
    switch_req.state = state;
    // device_id is optional, leave as 0
    
    // Encode the protobuf message
    pb_ostream_t stream = pb_ostream_from_buffer(&buffer[pos + 5], buffer_size - pos - 5); // Reserve space for length varint
    
    if (!pb_encode(&stream, SwitchCommandRequest_fields, &switch_req)) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Failed to encode SwitchCommandRequest: %s", PB_GET_ERROR(&stream));
#else
        printf("[%s] Failed to encode SwitchCommandRequest: %s\n", TAG, PB_GET_ERROR(&stream));
#endif
        return -1;
    }
    
    // Encode message type
    uint8_t msg_type_buf[5];
    int msg_type_len = _encode_varint(msg_type_buf, ESPHOME_MSG_SWITCH_COMMAND_REQUEST);
    
    // Encode data length
    uint32_t data_len = stream.bytes_written;
    int length_len = _encode_varint(&buffer[pos], data_len);
    
    // Move message type after length
    memmove(&buffer[pos + length_len], msg_type_buf, msg_type_len);
    
    // Copy encoded protobuf data after message type
    memmove(&buffer[pos + length_len + msg_type_len], &buffer[pos + 5], stream.bytes_written);
    
    // Calculate total message length
    int total_len = pos + length_len + msg_type_len + stream.bytes_written;
    
    return total_len;
}

#if 0
/**
 * @brief Initialize Noise cipher
 */
static void noise_cipher_init(noise_cipher_t *cipher, const uint8_t *key) {
    if (key) {
        memcpy(cipher->key, key, 32);
    } else {
        memset(cipher->key, 0, 32);
    }
    cipher->nonce = 0;
    cipher->initialized = (key != NULL);
}

/**
 * @brief Generate DH keypair using Curve25519
 */
static int noise_dh_generate_keypair(uint8_t *private_key, uint8_t *public_key) {
    // Generate random private key
    randombytes_buf(private_key, 32);
    
    // Clamp the private key for Curve25519
    private_key[0] &= 248;
    private_key[31] &= 127;
    private_key[31] |= 64;
    
    // Generate public key
    crypto_scalarmult_curve25519_base(public_key, private_key);
    
    return 0;
}

/**
 * @brief Compute shared secret using Curve25519
 */
static int noise_dh_shared_secret(const uint8_t *private_key, const uint8_t *public_key, uint8_t *shared_secret) {
    return crypto_scalarmult_curve25519(shared_secret, private_key, public_key);
}
/**
 * @brief Free noise handshake resources
 */
int noise_handshake_free(noise_handshake_t *handshake) {
    // Clear sensitive data
    sodium_memzero(handshake, sizeof(noise_handshake_t));
    return 0;
}

/**
 * @brief Simple HKDF implementation using HMAC-SHA256
 */
static void hkdf_sha256(const uint8_t *key, size_t key_len, const uint8_t *salt, size_t salt_len, 
                       const uint8_t *info, size_t info_len, uint8_t *output, size_t output_len) {
    uint8_t prk[32];
    
    // Extract phase: PRK = HMAC-SHA256(salt, key)
    crypto_auth_hmacsha256_state state;
    crypto_auth_hmacsha256_init(&state, salt, salt_len);
    crypto_auth_hmacsha256_update(&state, key, key_len);
    crypto_auth_hmacsha256_final(&state, prk);
    
    // Expand phase: generate output_len bytes
    uint8_t t[32];
    uint8_t counter = 1;
    size_t generated = 0;
    
    while (generated < output_len) {
        crypto_auth_hmacsha256_init(&state, prk, 32);
        if (generated > 0) {
            crypto_auth_hmacsha256_update(&state, t, 32);
        }
        crypto_auth_hmacsha256_update(&state, info, info_len);
        crypto_auth_hmacsha256_update(&state, &counter, 1);
        crypto_auth_hmacsha256_final(&state, t);
        
        size_t to_copy = (output_len - generated > 32) ? 32 : (output_len - generated);
        memcpy(output + generated, t, to_copy);
        generated += to_copy;
        counter++;
    }
}

// ============================================================================
// Noise Protocol Implementation
// ============================================================================

// ============================================================================
// Noise Protocol Implementation
// ============================================================================

/**
 * @brief Initialize Noise handshake
 */
int noise_handshake_init(noise_handshake_t *handshake, const uint8_t *psk) {
    if (!handshake || !psk) return -1;

    memset(handshake, 0, sizeof(*handshake));
    handshake->state = NOISE_STATE_INIT;

    // Set prologue
    memcpy(handshake->prologue, NOISE_PROLOGUE, strlen(NOISE_PROLOGUE));
    handshake->prologue_len = strlen(NOISE_PROLOGUE);

    // Copy PSK
    memcpy(handshake->psk, psk, 32);

    // For bypass, set shared_secret to PSK
    memcpy(handshake->shared_secret, psk, 32);

    // Initialize hash input with protocol name
    handshake->hash_input_len = 0;
    memcpy(handshake->hash_input, NOISE_PROTOCOL_NAME, strlen(NOISE_PROTOCOL_NAME));
    handshake->hash_input_len += strlen(NOISE_PROTOCOL_NAME);

    return 0;
}

/**
 * @brief Create client hello message
 */
int noise_create_client_hello(noise_handshake_t *handshake, uint8_t *buffer, size_t *buffer_len) {
    printf("UNIQUE_DEBUG_V2: noise_create_client_hello called with state = %d\n", handshake ? handshake->state : -1);
    fflush(stdout);
    if (!handshake || !buffer || !buffer_len) {
        printf("UNIQUE_DEBUG_V2: Invalid parameters\n");
        fflush(stdout);
        return -1;
    }
    if (handshake->state != NOISE_STATE_INIT) {
        printf("UNIQUE_DEBUG_V2: Wrong state %d, expected %d\n", handshake->state, NOISE_STATE_INIT);
        fflush(stdout);
        return -1;
    }

    printf("UNIQUE_DEBUG_V2: Generating keypair\n");
    fflush(stdout);
    // Generate ephemeral keypair
    noise_dh_generate_keypair(handshake->e_private, handshake->e_public);

    // Update hash with prologue
    memcpy(&handshake->hash_input[handshake->hash_input_len], handshake->prologue, handshake->prologue_len);
    handshake->hash_input_len += handshake->prologue_len;

    // Update hash with client ephemeral public key
    memcpy(&handshake->hash_input[handshake->hash_input_len], handshake->e_public, 32);
    handshake->hash_input_len += 32;

    // Client hello is just the ephemeral public key
    memcpy(buffer, handshake->e_public, 32);
    *buffer_len = 32;

    printf("UNIQUE_DEBUG_V2: Set buffer_len to 32, first byte of buffer = 0x%02x\n", buffer[0]);
    fflush(stdout);

    handshake->state = NOISE_STATE_HELLO;

    return 0;
}

/**
 * @brief Process server hello message (client side)
 */
int noise_process_server_hello(noise_handshake_t *handshake, const uint8_t *data, size_t data_len) {
    if (!handshake || !data || data_len != 32) return -1;
    if (handshake->state != NOISE_STATE_HELLO) return -1;

    // Copy server ephemeral public key
    memcpy(handshake->re_public, data, 32);

    // Update hash with server ephemeral public key
    memcpy(&handshake->hash_input[handshake->hash_input_len], handshake->re_public, 32);
    handshake->hash_input_len += 32;

    // Compute shared secret: DH(e_private, re_public)
    if (crypto_scalarmult(handshake->shared_secret, handshake->e_private, handshake->re_public) != 0) {
        return -1;
    }

    // Update hash with shared secret
    memcpy(&handshake->hash_input[handshake->hash_input_len], handshake->shared_secret, 32);
    handshake->hash_input_len += 32;

    // Update hash state with shared secret
    crypto_generichash_blake2b_update(&handshake->hash_state, handshake->shared_secret, 32);

    // Get final hash for key derivation
    uint8_t hash[32];
    crypto_generichash_blake2b_final(&handshake->hash_state, hash, 32);

    // Derive keys using HKDF-like construction
    uint8_t key_material[32];
    hkdf_sha256(key_material, handshake->hash_input, handshake->hash_input_len, (const uint8_t*)"NoiseServerKey", 14);

    // Split keys for send and receive
    uint8_t send_key[32], recv_key[32];
    hkdf_sha256(send_key, key_material, 32, (const uint8_t*)"NoiseSendKey", 12);
    hkdf_sha256(recv_key, key_material, 32, (const uint8_t*)"NoiseRecvKey", 12);

    // Initialize ciphers
    noise_cipher_init(&handshake->send_cipher, send_key);
    noise_cipher_init(&handshake->recv_cipher, recv_key);

    handshake->state = NOISE_STATE_HANDSHAKE;
    return 0;
}
    // send_key = HKDF(hash, shared_secret, "send")
    // recv_key = HKDF(hash, shared_secret, "recv")
    uint8_t send_key[32], recv_key[32];
    crypto_generichash_blake2b_state hkdf_state;

    // Send key
    crypto_generichash_blake2b_init(&hkdf_state, NULL, 0, 32);
    crypto_generichash_blake2b_update(&hkdf_state, handshake->psk, 32);
    crypto_generichash_blake2b_update(&hkdf_state, (const uint8_t*)"send", 4);
    crypto_generichash_blake2b_final(&hkdf_state, send_key, 32);

    // Recv key
    crypto_generichash_blake2b_init(&hkdf_state, NULL, 0, 32);
    crypto_generichash_blake2b_update(&hkdf_state, handshake->psk, 32);
    crypto_generichash_blake2b_update(&hkdf_state, (const uint8_t*)"recv", 4);
    crypto_generichash_blake2b_final(&hkdf_state, recv_key, 32);

    // Initialize cipher states
    memcpy(handshake->send_cipher_state.key, send_key, 32);
    handshake->send_cipher_state.nonce = 0;
    handshake->send_cipher_state.initialized = true;

    memcpy(handshake->recv_cipher_state.key, recv_key, 32);
    handshake->recv_cipher_state.nonce = 0;
    handshake->recv_cipher_state.initialized = true;

    handshake->state = NOISE_STATE_HANDSHAKE;

    return 0;
}

/**
 * @brief Create client auth message
 */
int noise_create_client_auth(noise_handshake_t *handshake, uint8_t *buffer, size_t *buffer_len) {
    if (!handshake || !buffer || !buffer_len) return -1;
    if (handshake->state != NOISE_STATE_HANDSHAKE) return -1;

    // Client auth is empty for NNpsk0 pattern
    *buffer_len = 0;
    handshake->state = NOISE_STATE_READY;

    return 0;
}

/**
 * @brief Process client hello (server side)
 */
int noise_process_client_hello(noise_handshake_t *handshake, const uint8_t *data, size_t data_len) {
    if (!handshake || !data || data_len != 32) return -1;
    if (handshake->state != NOISE_STATE_INIT) return -1;

    // Store client hello data for prologue
    memcpy(handshake->client_hello_data, data, data_len);
    handshake->client_hello_len = data_len;

    // Extract client's ephemeral public key
    memcpy(handshake->re_public, data, 32);

    handshake->state = NOISE_STATE_CLIENT_HELLO_RECEIVED;

    return 0;
}

/**
 * @brief Create server hello message
 */
int noise_create_server_hello(noise_handshake_t *handshake, const uint8_t *psk, uint8_t *output, size_t *output_len) {
    if (!handshake || !psk || !output || !output_len || *output_len < 32) return -1;
    if (handshake->state != NOISE_STATE_CLIENT_HELLO_RECEIVED) return -1;

    // Generate ephemeral keypair
    noise_dh_generate_keypair(handshake->e_private, handshake->e_public);

    // Initialize hash state with protocol name
    crypto_generichash_blake2b_init(&handshake->hash_state, NULL, 0, 32);
    crypto_generichash_blake2b_update(&handshake->hash_state, (const uint8_t*)NOISE_PROTOCOL_NAME, strlen(NOISE_PROTOCOL_NAME));

    // Update hash with prologue
    crypto_generichash_blake2b_update(&handshake->hash_state, handshake->prologue, handshake->prologue_len);

    // Update hash with client ephemeral public key
    crypto_generichash_blake2b_update(&handshake->hash_state, handshake->re_public, 32);

    // Update hash with server ephemeral public key
    crypto_generichash_blake2b_update(&handshake->hash_state, handshake->e_public, 32);

    // Server hello is just the ephemeral public key
    memcpy(output, handshake->e_public, 32);
    *output_len = 32;

    handshake->state = NOISE_STATE_SERVER_HELLO_SENT;

    return 0;
}

/**
 * @brief Process client auth (server side)
 */
int noise_process_client_auth(noise_handshake_t *handshake, const uint8_t *psk, const uint8_t *data, size_t data_len) {
    if (!handshake || !psk || data_len != 0) return -1;
    if (handshake->state != NOISE_STATE_SERVER_HELLO_SENT) return -1;

    // Compute shared secret: DH(e_private, re_public)
    if (crypto_scalarmult(handshake->shared_secret, handshake->e_private, handshake->re_public) != 0) {
        return -1;
    }

    // Update hash with shared secret
    crypto_generichash_blake2b_update(&handshake->hash_state, handshake->shared_secret, 32);

    // Get final hash for key derivation
    uint8_t hash[32];
    crypto_generichash_blake2b_final(&handshake->hash_state, hash, 32);

    // Derive keys using HKDF-like construction
    uint8_t send_key[32], recv_key[32];
    crypto_generichash_blake2b_state hkdf_state;

    // Send key (server perspective)
    crypto_generichash_blake2b_init(&hkdf_state, NULL, 0, 32);
    crypto_generichash_blake2b_update(&hkdf_state, hash, 32);
    crypto_generichash_blake2b_update(&hkdf_state, handshake->shared_secret, 32);
    crypto_generichash_blake2b_update(&hkdf_state, (const uint8_t*)"recv", 4);  // Server sends with "recv" key
    crypto_generichash_blake2b_final(&hkdf_state, send_key, 32);

    // Recv key (server perspective)
    crypto_generichash_blake2b_init(&hkdf_state, NULL, 0, 32);
    crypto_generichash_blake2b_update(&hkdf_state, hash, 32);
    crypto_generichash_blake2b_update(&hkdf_state, handshake->shared_secret, 32);
    crypto_generichash_blake2b_update(&hkdf_state, (const uint8_t*)"send", 4);  // Server receives with "send" key
    crypto_generichash_blake2b_final(&hkdf_state, recv_key, 32);

    // Initialize cipher states
    memcpy(handshake->send_cipher_state.key, send_key, 32);
    handshake->send_cipher_state.nonce = 0;
    handshake->send_cipher_state.initialized = true;

    memcpy(handshake->recv_cipher_state.key, recv_key, 32);
    handshake->recv_cipher_state.nonce = 0;
    handshake->recv_cipher_state.initialized = true;

    handshake->state = NOISE_STATE_READY;

    return 0;
}

/**
 * @brief Encrypt message using ChaCha20-Poly1305
 */
static int noise_encrypt(noise_cipher_t *cipher, const uint8_t *plaintext, size_t plaintext_len,
                        uint8_t *ciphertext, size_t *ciphertext_len) {
    if (!cipher || !cipher->initialized || !plaintext || !ciphertext || !ciphertext_len) return -1;

    // Use libsodium's ChaCha20-Poly1305
    uint8_t nonce[12] = {0};
    memcpy(nonce, &cipher->nonce, 8);  // Little-endian nonce

    if (crypto_aead_chacha20poly1305_encrypt(ciphertext, (unsigned long long *)ciphertext_len,
                                           plaintext, plaintext_len,
                                           NULL, 0,  // no additional data
                                           NULL, nonce, cipher->key) != 0) {
        return -1;
    }

    cipher->nonce++;
    return 0;
}

/**
 * @brief Decrypt message using ChaCha20-Poly1305
 */
static int noise_decrypt(noise_cipher_t *cipher, const uint8_t *ciphertext, size_t ciphertext_len,
                        uint8_t *plaintext, size_t *plaintext_len) {
    if (!cipher || !cipher->initialized || !ciphertext || !plaintext || !plaintext_len) return -1;

    uint8_t nonce[12] = {0};
    memcpy(nonce, &cipher->nonce, 8);  // Little-endian nonce

    if (crypto_aead_chacha20poly1305_decrypt(plaintext, (unsigned long long *)plaintext_len,
                                           NULL,  // no additional data
                                           ciphertext, ciphertext_len,
                                           NULL, 0, nonce, cipher->key) != 0) {
        return -1;
    }

    cipher->nonce++;
    return 0;
}
#endif

// Stubs for noise functions
int noise_handshake_init(noise_handshake_t *handshake, const uint8_t *psk) { return -1; }
int noise_create_client_hello(noise_handshake_t *handshake, uint8_t *buffer, size_t *buffer_len) { return -1; }
int noise_process_server_hello(noise_handshake_t *handshake, const uint8_t *data, size_t data_len) { return -1; }
int noise_create_client_auth(noise_handshake_t *handshake, uint8_t *buffer, size_t *buffer_len) { return -1; }
int noise_process_client_hello(noise_handshake_t *handshake, const uint8_t *data, size_t data_len) { return -1; }
int noise_create_server_hello(noise_handshake_t *handshake, const uint8_t *psk, uint8_t *output, size_t *output_len) { return -1; }
int noise_process_client_auth(noise_handshake_t *handshake, const uint8_t *psk, const uint8_t *data, size_t data_len) { return -1; }
int noise_handshake_free(noise_handshake_t *handshake) { return 0; }

/** * @brief Create ESPHome hello response
 */
int esphome_create_hello_response(uint8_t *buffer, size_t buffer_size, const char *server_name) {
    HelloResponse response = HelloResponse_init_zero;
    response.api_version_major = 1;
    response.api_version_minor = 9;
    // response.server_info = "ESPHome";
    // response.name = server_name;
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    if (!pb_encode(&stream, HelloResponse_fields, &response)) {
        return -1;
    }
    return stream.bytes_written;
}

/** * @brief Send encrypted ESPHome message
 */
int esphome_send_encrypted_message(int socket_fd, noise_handshake_t *handshake,
                                  uint32_t message_type, const uint8_t *data, size_t data_len) {
    return -1;
}

/**
 * @brief Read encrypted ESPHome message
 */
int esphome_read_encrypted_message(int socket_fd, noise_handshake_t *handshake,
                                  uint8_t *buffer, size_t buffer_size, uint32_t *message_type) {
    return -1;
}

/**
 * @brief Encode ESPHome message with protobuf data (stub implementation)
 */
int esphome_encode_message(uint8_t *buffer, size_t buffer_len, uint32_t message_type, const void *data) {
    // Simple stub for testing - just encode message type
    if (buffer_len < 4) return -1;
    memcpy(buffer, &message_type, 4);
    return 4;
}

/**
 * @brief Decode ESPHome message from protobuf data (stub implementation)
 */
int esphome_decode_message(const uint8_t *buffer, size_t buffer_len, uint32_t *message_type, void *data) {
    // Simple stub for testing - just decode message type
    if (buffer_len < 4) return -1;
    memcpy(message_type, buffer, 4);
    return 0;
}

/**
 * @brief Send unencrypted ESPHome message
 */
int esphome_send_unencrypted_message(int socket_fd, uint32_t message_type, const uint8_t *data, size_t data_len) {
    uint8_t buffer[1024];
    int pos = 0;

    // Preamble 0x00 for unencrypted
    buffer[pos++] = 0x00;

    // Encode message length (big-endian)
    uint16_t msg_len = 0;
    uint8_t *len_ptr = &buffer[pos];
    pos += 2;  // Skip length for now

    // Encode message type as varint
    int type_len = _encode_varint(&buffer[pos], message_type);
    msg_len += type_len;
    pos += type_len;

    // Encode data length as varint
    int data_len_encoded = _encode_varint(&buffer[pos], data_len);
    msg_len += data_len_encoded;
    pos += data_len_encoded;

    // Add data
    if (pos + data_len > sizeof(buffer)) return -1;
    memcpy(&buffer[pos], data, data_len);
    msg_len += data_len;
    pos += data_len;

    // Set message length
    len_ptr[0] = (msg_len >> 8) & 0xFF;
    len_ptr[1] = msg_len & 0xFF;

    if (send(socket_fd, buffer, pos, 0) != pos) {
        return -1;
    }

    return 0;
}

/**
 * @brief Send Noise hello as raw data with NOISE_HELLO prefix (matching aioesphomeapi)
 */
int esphome_send_hello_with_noise(int socket_fd, const uint8_t *data, size_t data_len) {
    // Send NOISE_HELLO prefix like aioesphomeapi does
    uint8_t noise_hello[3] = {0x01, 0x00, 0x00};
    if (send(socket_fd, noise_hello, 3, 0) != 3) {
        return -1;
    }
    
    // Calculate frame length: data_len + 1 for the trailing \x00
    uint16_t frame_len = data_len + 1;
    
    // Send header with preamble 0x01 and big-endian length
    uint8_t header[3];
    header[0] = 0x01;  // preamble
    header[1] = (frame_len >> 8) & 0xFF;  // big-endian length
    header[2] = frame_len & 0xFF;
    if (send(socket_fd, header, 3, 0) != 3) {
        return -1;
    }
    
    // Send the \x00 byte
    uint8_t zero = 0x00;
    if (send(socket_fd, &zero, 1, 0) != 1) {
        return -1;
    }
    
    // Send the handshake data
    if (send(socket_fd, data, data_len, 0) != (ssize_t)data_len) {
        return -1;
    }
    
    return 0;
}
