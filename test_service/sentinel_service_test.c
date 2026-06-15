#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "comm/noonlight.h"

int main() {
    printf("=== STARTING SENTINEL SERVICE TEST ===\n");

    // 1. Setup Mock config with Credentials
    struct config my_home = {
        .name = "kjgerhart",
        .phone = "2165551212",
        .pin = "1234",
        .address1 = "1234 Security Lane",
        .zip = "44139",
        .monitor_police = true,
        .monitor_fire = true
    };
    
    // Using your specific fixed-length character arrays
    strncpy(my_home.MonitorServiceID, "https://api-sandbox.noonlight.com/dispatch/v1/alarms", 63);
    strncpy(my_home.MonitorServiceKey, "sandbox_bearer_token_999888777", 127);

    // 2. Setup Mock Contacts
    struct contact list[2] = {
        {.name = "Primary Backup", .phone = "2165550001", .pin = "4321", .notify = "service"},
        {.name = "Neighbor", .phone = "2165550002", .pin = "0000", .notify = "app"} // Should NOT sync
    };

    // 3. Scenario: Front Door Tripped (First Event)
    struct zone front_door = {
        .location = "Front Door",
        .type = "police",
        .mfg = "Sentinel",
        .model = "V1-Pro",
        .description = "Door Opened"
    };

    printf("\n--- SCENARIO: INITIAL TRIGGER (Front Door) ---\n");
    noonlight_handle_trigger(&my_home, &front_door, list, 2);

    // Verify id was captured
    char *police_id = noonlight_get_active_id(SLOT_POLICE);
    if (police_id != NULL) {
        printf("\nVERIFICATION: Police Alarm Active with id: %s\n", police_id);
    } else {
        printf("\nVERIFICATION FAILED: No Police id captured.\n");
        return 1;
    }

    // 4. Scenario: Living Room Motion (Second Event while Alarm is active)
    struct zone living_room = {
        .location = "Living Room",
        .type = "police",
        .mfg = "Sentinel",
        .model = "M1-Motion",
        .description = "Motion Detected"
    };

    printf("\n--- SCENARIO: SUBSEQUENT TRIGGER (Living Room) ---\n");
    noonlight_handle_trigger(&my_home, &living_room, list, 2);

    // 5. Scenario: User Disarms (Cancellation)
    printf("\n--- SCENARIO: USER DISARM (PIN Entered) ---\n");
    noonlight_cancel_alarm(&my_home, SLOT_POLICE, "1234");

    // Verify slot is cleared
    if (noonlight_get_active_id(SLOT_POLICE) == NULL) {
        printf("\nVERIFICATION: Police Slot cleared successfully.\n");
    }

    printf("\n=== SENTINEL SERVICE TEST COMPLETE ===\n");
    return 0;
}