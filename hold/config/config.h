#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>

// --- System Capacity ---
#define MAX_ZONES   32
#define MAX_USERS   10
#define MAX_RELAYS  8
#define MAX_RULES   20

// --- Runtime Enums ---

typedef enum {
    ARMSTATE_DISARMED,
    ARMSTATE_STAY,
    ARMSTATE_AWAY,
    ARMSTATE_NIGHT,
    ARMSTATE_VACATION
} ArmState_t;

typedef enum {
    ALARM_IDLE,         // Normal operation
    ALARM_EXITING,      // Exit delay active (Buzzer 1Hz)
    ALARM_PENDING,      // Entry delay active (Buzzer 2Hz)
    ALARM_TRIGGERED,    // Active alarm (Siren ON, Noonlight Sent)
    ALARM_CANCELLED,    // Silenced, waiting for reset
    ALARM_TROUBLE       // Hardware/Comms failure
} AlarmStatus_t;

// --- Data Structures ---

typedef struct {
    char account_id[64];
    char pin[8];
    char name[64];
    char address1[64];
    char address2[64];
    char city[32];
    char state[16];
    char zip_code[12];
    char email[64];
    char phone[20];
    char instructions[128];
    double latitude;
    double longitude;
    double accuracy;
//    char MonitorServiceID[64];
    char monitor_service_key[128];
    char monitoring_url[128];
    int notify;         // none, email, telegram, service
    bool is_monitor_fire;
    bool is_monitor_police;
    bool is_monitor_medical;
    bool is_monitor_other;
    char smtp_server[64];
    int smtp_port;
    char smtp_user[64];
    char smtp_pass[64];
    char mqtt_server[64];
    int mqtt_port;
    char mqtt_user[64];
    char mqtt_pass[64];
    char telegram_id[32];
    char telegram_token[128];
    bool is_telegram_enabled;
    char nvr_server_url[128];
    char ha_integration_url[128];
    int is_entry_delay;
    int is_exit_delay;
    int is_cancel_delay;
} config_t;

typedef struct {
    int id;
    char name[32];
    char pin[8];
    char phone[20];
    char email[64];
    char notify[16];
    bool is_admin;
} User_t;

typedef struct {
    int id;
    char name[32];
    char description[64];
    char type[16];           // fire, police, medical, other
    char location[32];
    char model[32];
    char manufacturer[32];
    bool is_chime;
    bool is_alarm_on_armed_pnly;   // false = 24-hour (Smokes/Panic)
    int gpio;
    bool is_i2c;
    uint8_t i2c_address;
    bool is_perimeter;
    bool is_interior;
    bool is_anic;
} Zone_t;

typedef struct {
    int id;
    char name[32];
    char description[64];
    int duration;
    char location[32];
    char type[16];           // siren, strobe, light, lock
    bool is_repeat;
    int gpio;
} Relay_t;

typedef struct {
    int id;
    char name[32];
    bool is_enabled;
    int trigger_zone_id;       // Link to Zone_t.id
    char trigger_condition[16]; // "open", "closed"
    ArmState_t required_state; // Only run if in this state
    int action_relay_id;       // Link to Relay_t.id
    int action_type;          // 0:Toggle, 1:Momentary
    int duration;
} Rule_t;

#endif