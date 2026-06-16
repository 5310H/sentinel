#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "dispatcher.h"
#include "storage_mgr.h"
#include "user_mgr.h"
#include "smtp.h"
#include "noonlight.h"
#include "telegram.h"
#include "logging.h"

#ifdef ESP_PLATFORM
    #include "esp_log.h"
    static const char *TAG = "DISPATCHER";
#else
    static const char *TAG = "DISPATCHER";
#endif

// External globals from storage_mgr

// --- CORE DISPATCH LOGIC ---

void dispatcher_alert(zone_t *z) {
    if (z == NULL || z->name[0] == '\0') return;
    
    int monitoring_enabled = 0;
    const char* category = "UNKNOWN";
    zone_call_t zone_call = zone_call_from_string(z->call);

    // STEP 1: GATEKEEPER (Config check)
    if (zone_call == ZONE_CALL_FIRE) {
        category = "FIRE";
        monitoring_enabled = storage_get_config()->is_monitor_fire;
    } 
    else if (zone_call == ZONE_CALL_POLICE) {
        category = "POLICE";
        monitoring_enabled = storage_get_config()->is_monitor_police;
    } 
    else if (zone_call == ZONE_CALL_MEDICAL) {
        category = "MEDICAL";
        monitoring_enabled = storage_get_config()->is_monitor_medical;
    } 
    else {
        category = "OTHER";
        monitoring_enabled = storage_get_config()->is_monitor_other;
    }

    if (!monitoring_enabled) {
        LOG_INFO(TAG, "Gatekeeper Veto: %s monitoring is disabled.", category);
        return; 
    }

    // STEP 2: ROUTING BASED ON NOTIFY TYPE
    LOG_WARN(TAG, "Dispatching alert for %s (Method: %d)", z->name, storage_get_config()->notify);

    switch(storage_get_config()->notify) {
        case 1: // NONE
            printf("[%s] Notify set to NONE. Alert suppressed.\n", TAG);
            break;

        case 2: // SMTP / EMAIL
            smtp_alert_all_contacts(storage_get_config(), z);
            break;

        case 3: // TELEGRAM
            telegram_send_alert(storage_get_config(), z);
            break;

        case 4: // NOONLIGHT (Professional Monitoring)
            {
                int slot = SLOT_OTH; // Default to Slot 3 (Panic/Other)
                zone_call_t zone_call = zone_call_from_string(z->call);
                
                if (zone_call == ZONE_CALL_FIRE)     slot = SLOT_FIRE;   // Slot 0
                else if (zone_call == ZONE_CALL_POLICE)  slot = SLOT_POLICE; // Slot 1
                else if (zone_call == ZONE_CALL_MEDICAL) slot = SLOT_MED;    // Slot 2

                // Only trigger if a dispatch isn't already active in this slot
                if (strlen(storage_get_config()->nl_ids[slot]) == 0) {
                    noonlight_create_alarm(storage_get_config(), z, slot);
                } else {
                    LOG_DEBUG(TAG, "Noonlight slot %d already active. Skipping duplicate.", slot);
                }
            }
            break;

        default:
            printf("[%s] Unknown notify type: %d\n", TAG, storage_get_config()->notify);
            break;
    }
}

void dispatcher_cancel_alert(void) {
    printf("[%s] Global Cancellation requested.\n", TAG);

    if (storage_get_config()->notify == 4) {
        // Attempt to cancel all 4 possible Noonlight slots (Fire, Police, Med, Other)
        for (int i = 0; i < MAX_ALARM_SLOTS && i < 4; i++) {
            if (storage_get_config()->nl_ids[i][0] != '\0' && strlen(storage_get_config()->nl_ids[i]) > 0) {
                noonlight_cancel_alarm(storage_get_config(), storage_get_config()->nl_ids[i]);
                // Note: ID is cleared by noonlight.c/sync_task once confirmed by cloud
            }
        }
    } 
    else if (storage_get_config()->notify == 3) {
        telegram_send_cancellation(storage_get_config());
    }
    else if (storage_get_config()->notify == 2) {
        smtp_send_cancellation(storage_get_config());
    }
}