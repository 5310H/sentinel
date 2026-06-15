#include <stdio.h>
#include "dispatch.h"

void transport_send(int slot) {
    const char* names[] = {"NOONLIGHT", "TELEGRAM", "SMTP"};
    printf("\n[MOCK HW] Sending %s Packet...\n", names[slot]);
    printf("DATA: %s\n", payload_buffer);

    // Simulate Network Response
    char mock_id[64] = "MOCK_ID_999";
    char* mock_responses[] = {
        "{\"id\":\"MOCK_ID_999\",\"status\":\"active\"}",
        "{\"ok\":true}",
        "250 Message accepted"
    };

    char captured_id[64] = "";
    bool ok = payload_parse_response(slot, mock_responses[slot], captured_id);
    
    if (ok) {
        printf("[MOCK HW] Response Success. id: %s\n", captured_id);
        dispatch_on_callback(slot, true, captured_id);
    }
}