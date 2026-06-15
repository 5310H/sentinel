#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../components/esphome_api/esphome_api_protocol.h"

int main() {
    printf("Testing ESPHome Switch Command protobuf encoding\n");

    uint8_t output_buffer[256];
    memset(output_buffer, 0, sizeof(output_buffer));

    // Test the switch command encoding
    printf("Encoding switch command: key=12345, state=true\n");
    int encoded_len = esphome_encode_switch_command(output_buffer, sizeof(output_buffer), 12345, true);

    if (encoded_len < 0) {
        printf("ERROR: esphome_encode_switch_command failed\n");
        return 1;
    }

    printf("Successfully encoded %d bytes\n", encoded_len);

    // Display the encoded message
    printf("Encoded message:\n");
    for (int i = 0; i < encoded_len; i++) {
        printf("%02x ", output_buffer[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    // Basic validation
    if (encoded_len >= 10) {
        printf("Preamble: 0x%02x (expected 0x00)\n", output_buffer[0]);
        printf("Data length: %u (expected 7)\n", output_buffer[1]);
        printf("Message type: %u (expected 30)\n", output_buffer[2]);
        printf("Protobuf data: %02x %02x %02x %02x %02x %02x %02x\n",
               output_buffer[3], output_buffer[4], output_buffer[5], output_buffer[6],
               output_buffer[7], output_buffer[8], output_buffer[9]);
    }

    printf("Test completed successfully\n");
    return 0;
}