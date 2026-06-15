#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../nanopb/pb_encode.h"
#include "../nanopb/pb_decode.h"
#include "../generated_proto/api.pb.h"

// Simple test of protobuf encoding without encryption
int main() {
    printf("Testing ESPHome protobuf encoding\n");

    // Test SwitchCommand encoding directly
    uint8_t buffer[256];
    
    // Create SwitchCommandRequest message
    SwitchCommandRequest switch_req = SwitchCommandRequest_init_zero;
    switch_req.key = 12345;
    switch_req.state = true;
    
    // Encode the protobuf message
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    
    if (!pb_encode(&stream, SwitchCommandRequest_fields, &switch_req)) {
        printf("ERROR: Failed to encode SwitchCommandRequest: %s\n", PB_GET_ERROR(&stream));
        return 1;
    }
    
    printf("Encoded protobuf (%lu bytes): ", stream.bytes_written);
    for (size_t i = 0; i < stream.bytes_written && i < 32; i++) {
        printf("%02x ", buffer[i]);
    }
    printf("\n");
    
    // Test decoding
    SwitchCommandRequest decoded = SwitchCommandRequest_init_zero;
    pb_istream_t istream = pb_istream_from_buffer(buffer, stream.bytes_written);
    
    if (!pb_decode(&istream, SwitchCommandRequest_fields, &decoded)) {
        printf("ERROR: Failed to decode SwitchCommandRequest: %s\n", PB_GET_ERROR(&istream));
        return 1;
    }
    
    printf("Decoded: key=%u, state=%s\n", (unsigned int)decoded.key, decoded.state ? "true" : "false");
    
    if (decoded.key == 12345 && decoded.state == true) {
        printf("SUCCESS: Protobuf encoding/decoding works correctly\n");
        return 0;
    } else {
        printf("ERROR: Decoded values don't match\n");
        return 1;
    }
}