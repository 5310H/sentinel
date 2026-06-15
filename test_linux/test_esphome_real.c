#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../components/esphome_api/esphome_api_client.h"
#include "../components/storage/storage_mgr.h"

int main() {
    printf("Testing ESPHome API connection to 192.168.69.209\n");

    // Initialize storage and load configuration
    storage_init();
    storage_load_all();

    // Initialize
    if (esphome_api_init() != 0) {
        printf("Failed to initialize ESPHome API\n");
        return 1;
    }

    // Try different entity keys
    uint32_t entity_keys[] = {1, 9, 10, 12345, 4115113443};
    int num_keys = sizeof(entity_keys) / sizeof(entity_keys[0]);

    // Use the configured hostname from the first device
    const char *hostname = "192.168.69.209"; // Use IP address directly since .local doesn't work
    uint16_t port = 6053;

    for (int i = 0; i < num_keys; i++) {
        printf("\n--- Testing entity_key=%u on %s ---\n", entity_keys[i], hostname);

        // Connect using configured hostname (will use encryption key from config)
        int result = esphome_api_connect(hostname, port, NULL, NULL);
        if (result == 0) {
            printf("Connection successful! Attempting to toggle relay...\n");
            
            // Try to set switch ON
            result = esphome_api_set_switch(hostname, entity_keys[i], 1);
            printf("esphome_api_set_switch(ON) returned: %d\n", result);
            
            if (result == 0) {
                printf("SUCCESS: Relay toggled ON!\n");
                
                // Wait a moment
                sleep(2);
                
                // Try to set switch OFF
                result = esphome_api_set_switch(hostname, entity_keys[i], 0);
                printf("esphome_api_set_switch(OFF) returned: %d\n", result);
                
                if (result == 0) {
                    printf("SUCCESS: Relay toggled OFF!\n");
                } else {
                    printf("FAILED: Could not toggle relay OFF\n");
                }
            } else {
                printf("FAILED: Could not toggle relay ON\n");
            }
            
            // Disconnect
            esphome_api_disconnect(hostname);
        } else {
            printf("Connection failed\n");
        }

        sleep(1); // Brief delay between tests
    }

    return 0;
}