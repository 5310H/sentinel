#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include "notifier.h"
#include "config.h"

#define NOONLIGHT_TOKEN     "YOUR_TOKEN"
#define NOONLIGHT_URL       "https://api-sandbox.noonlight.com/dispatch/v1/alarms"

static config_t* _owner = NULL;
static User_t* _users = NULL;
static int _u_count = 0;

void notifier_init(config_t* owner_ptr, User_t* users_ptr, int u_count) {
    _owner = owner_ptr;
    _users = users_ptr;
    _u_count = u_count;
    curl_global_init(CURL_GLOBAL_ALL);
}

void notifier_send(NotifyType_t type, const char* message) {
    if (type == NOTIFY_ALARM && _owner) {
        CURL *curl = curl_easy_init();
        if(curl) {
            struct curl_slist *headers = curl_slist_append(NULL, "Content-type: application/json");
            char auth[256]; snprintf(auth, sizeof(auth), "Authorization: Bearer %s", NOONLIGHT_TOKEN);
            headers = curl_slist_append(headers, auth);

            char json[2048];
            // Matches your config.h: Address1 and ZipCode
            snprintf(json, sizeof(json), 
                "{\"name\":\"%s\",\"phone\":\"%s\","
                "\"location\":{\"address\":{\"line1\":\"%s\",\"city\":\"%s\",\"state\":\"%s\",\"zip\":\"%s\"}},"
                "\"note\":\"%s\"}", 
                _owner->name, _owner->Phone, _owner->Address1, _owner->City, _owner->State, _owner->ZipCode,
                message);

            curl_easy_setopt(curl, CURLOPT_URL, NOONLIGHT_URL);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
            curl_easy_perform(curl);
            curl_easy_cleanup(curl);
        }

        // Email Loop
        for (int i = 0; i < _u_count; i++) {
            printf("[NOTIFIER] Sending Email to %s via %s\n", _users[i].Email, _owner->SMTPServer);
        }
    }
}
// Add this to the bottom of components/engine/notifier.c
bool notifier_cancel_alarm(void) {
    printf("[NOTIFIER] Alarm cancellation requested. Stopping alerts...\n");
    // In the future, this would send a DELETE request to Noonlight
    // or a 'System Disarmed' email to the users.
    return true; 
}