#include "mqtt_logic.h"
#include "mongoose.h"
#include "cJSON.h"
#include "storage_mgr.h"
#include "ha_mqtt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hal_esp32.h"

// --- HAL Wrapper Prototypes ---
extern int __wrap_hal_get_zone_state(int zone);
extern int __wrap_hal_get_relay_state(int relay);
extern void __wrap_hal_set_relay(int relay, bool state);

// --- External Global Variables ---
extern struct mg_mgr mgr;
extern bool g_network_up;
extern config_t config; 
extern int current_arm_state;
extern int current_alarm_status;

struct mg_connection *s_conn = NULL;
// --- Prototypes ---
void handle_received_command(struct mg_str topic, struct mg_str data);
void mqtt_publish_status(void);

// --- Mongoose Event Handler ---
static void mqtt_fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_MQTT_OPEN) {
        printf("[MQTT] Connected to %s:%d\n", config.mqtt_server, config.mqtt_port);
        
        // Publish Home Assistant discovery
        ha_mqtt_init();
        
        // Subscribe to sentinel topics
        struct mg_mqtt_opts sub_opts = {
            .topic = mg_str("sentinel/alarm/set"), // Match Alarmo command topic
            .qos = 1
        };
        mg_mqtt_sub(c, &sub_opts); 
        printf("[MQTT] Subscribed to %.*s\n", (int) sub_opts.topic.len, sub_opts.topic.buf);
        
        // Subscribe to Home Assistant command topics
        struct mg_mqtt_opts ha_opts = {.topic = mg_str("sentinel/command/#"), .qos = 1};
        mg_mqtt_sub(c, &ha_opts);
        printf("[MQTT] Subscribed to Home Assistant command topics\n");

        mqtt_publish_status();
        
    } else if (ev == MG_EV_MQTT_MSG) {
        struct mg_mqtt_message *mm = (struct mg_mqtt_message *) ev_data;
        printf("[MQTT] Msg received: %.*s\n", (int) mm->data.len, mm->data.buf);
        
        // Handle Home Assistant commands
        char topic_buf[256] = {0};
        if (mm->topic.len < sizeof(topic_buf)) {
            memcpy(topic_buf, mm->topic.buf, mm->topic.len);
            ha_mqtt_handle_command(topic_buf, (uint8_t *)mm->data.buf, mm->data.len);
        }
        
        handle_received_command(mm->topic, mm->data);

    } else if (ev == MG_EV_ERROR) {
        printf("[MQTT] Error: %s\n", (char *) ev_data);
    } else if (ev == MG_EV_CLOSE) {
        printf("[MQTT] Connection closed\n");
        s_conn = NULL;
    }
}

// --- Command Processing ---
void handle_received_command(struct mg_str topic, struct mg_str data) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%.*s", (int) data.len, data.buf);

    char command[32] = {0};
    char code[16] = {0};

    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *c = cJSON_GetObjectItem(root, "command");
        cJSON *p = cJSON_GetObjectItem(root, "code");
        if (c && cJSON_IsString(c)) strncpy(command, c->valuestring, sizeof(command)-1);
        if (p && cJSON_IsString(p)) strncpy(code, p->valuestring, sizeof(code)-1);
        cJSON_Delete(root);
    } else {
        strncpy(command, buf, sizeof(command)-1);
    }

    if (strcmp(command, "DISARM") == 0) {
        if (code[0] != '\0') {
            bool authorized = false;
            // Bounds checking: ensure i < u_count AND i < MAX_USERS
            int max_users = (u_count < MAX_USERS) ? u_count : MAX_USERS;
            for (int i = 0; i < max_users; i++) {
                // Check for null/empty entries
                if (users[i].pin[0] != '\0' && strcmp(code, users[i].pin) == 0) {
                    authorized = true;
                    printf("[SYSTEM] User %d (%s) authorized disarm.\n", i, users[i].name);
                    break;
                }
            }
            if (authorized) {
                current_arm_state = 0; // DISARMED
            } else {
                printf("[SYSTEM] ACCESS DENIED: Invalid PIN %s\n", code);
            }
        }
    } // FIXED: Missing brace closed here
    else if (strcmp(command, "ARM_AWAY") == 0) {
        current_arm_state = 2; // AWAY
    }
    else if (strncmp(command, "RELAY_", 6) == 0) {
        int r_num;
        if (sscanf(command, "RELAY_%d_ON", &r_num) == 1) __wrap_hal_set_relay(r_num, true);
        if (sscanf(command, "RELAY_%d_OFF", &r_num) == 1) __wrap_hal_set_relay(r_num, false);
    }

    mqtt_publish_status();
}

// --- Connection Logic ---
void start_sentinel_mqtt(void) {
    if (!g_network_up || s_conn != NULL) return;

    char clean_server[128];
    if (sscanf(config.mqtt_server, "%127s", clean_server) != 1) return;

    char full_url[256];
    bool is_secure = (config.mqtt_port == 8883);
    snprintf(full_url, sizeof(full_url), "%s://%s:%d", is_secure ? "mqtts" : "mqtt", clean_server, config.mqtt_port);

    struct mg_mqtt_opts opts = {
        .clean = true,
        .qos = 1,
        .topic = mg_str("sentinel/alarm/availability"),
        .message = mg_str("offline"),
        .user = mg_str(config.mqtt_user),
        .pass = mg_str(config.mqtt_pass),
        .keepalive = 60
    };

    printf("[MQTT] Attempting %s\n", full_url);
    s_conn = mg_mqtt_connect(&mgr, full_url, &opts, mqtt_fn, NULL);

    if (s_conn != NULL && is_secure) {
        struct mg_tls_opts tls_opts = {.name = mg_str(clean_server)}; 
        mg_tls_init(s_conn, &tls_opts);
    }
}

// --- Publishing Logic ---
void mqtt_publish_alert(const char *topic, const char *msg) {
    if (s_conn == NULL) return;
    struct mg_mqtt_opts pub_opts = {.topic = mg_str(topic), .message = mg_str(msg), .qos = 1};
    mg_mqtt_pub(s_conn, &pub_opts);
}

void mqtt_publish_status(void) {
    if (s_conn == NULL || !g_network_up) return;

    const char *alarmo_states[] = {"disarmed", "armed_home", "armed_away", "armed_night", "armed_vacation"};
    const char *current_state = alarmo_states[0];
    if (current_arm_state >= 0 && current_arm_state < 5) current_state = alarmo_states[current_arm_state];
    if (current_alarm_status == 3) current_state = "triggered";

    mqtt_publish_alert("sentinel/alarm/state", current_state);
    mqtt_publish_alert("sentinel/alarm/availability", "online");
    
    // Also publish to Home Assistant topics
    ha_mqtt_publish_state();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "arm_state", current_state);
    
    cJSON *z_arr = cJSON_CreateArray();
    cJSON *r_arr = cJSON_CreateArray();
    for (int i = 0; i < 8; i++) {
        cJSON_AddItemToArray(z_arr, cJSON_CreateNumber(__wrap_hal_get_zone_state(i)));
        cJSON_AddItemToArray(r_arr, cJSON_CreateNumber(__wrap_hal_get_relay_state(i)));
    }
    cJSON_AddItemToObject(root, "zones", z_arr);
    cJSON_AddItemToObject(root, "relays", r_arr);

    char *json_out = cJSON_PrintUnformatted(root);
    if (json_out) {
        mqtt_publish_alert("sentinel/alarm/attributes", json_out);
        free(json_out);
    }
    cJSON_Delete(root);
}