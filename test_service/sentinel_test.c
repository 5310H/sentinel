#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "dispatcher.h"

int main() {
    printf("--- SENTINEL STARTUP: SAFETY CHECK ---\n");

    config_t my_home = {0};
    zone_t my_zones[MAX_ZONES] = {0};
    user_t my_users[MAX_USER] = {0};
    int user_count = 0;

    storage_load_owner(&my_home);
    storage_load_zones(my_zones, MAX_ZONES);
    storage_load_users(my_users, &user_count);

    // Initial Safety Output
    printf("[CHECK] Notification Mode: %s\n", my_home.notify);
    if (strcmp(my_home.notify, "noonlight") != 0) {
        printf("[SAFE] Noonlight is DISABLED. Emergency services will NOT be called.\n");
    } else {
        printf("[WARN] Noonlight is ENABLED. Real dispatch sequences will trigger.\n");
    }
// Inside sentinel_test.c main
storage_load_owner(&my_home);
storage_load_users(my_users, &user_count); // 1. Load data into memory first

// 2. Now pass that memory to the dispatcher
dispatcher_init(&my_home, my_zones, MAX_ZONES, my_users, user_count);
    printf("\n[TEST] Setting state to ARMED_AWAY...\n");
    current_state = STATE_ARMED_AWAY;

    printf("\n[TEST] --- STEP 1: TRIGGERING ZONE 1 ---\n");
    dispatcher_process_event(EVENT_ZONE_TRIP, 1);

    printf("\n[TEST] --- STEP 2: SIMULATING PIN CANCEL ---\n");
    dispatcher_process_event(EVENT_CANCEL, 0);

    printf("\n--- TEST SEQUENCE FINISHED ---\n");
    return 0;
}
