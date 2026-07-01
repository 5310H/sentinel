#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "dispatcher.h"
#include "payload_manager.h"
#include "storage_mgr.h"

char payload_buffer[1024];

// CHANGE: struct config* -> config_t* |  struct zone* -> zone_t*
void payload_build_noonlight(config_t *o, zone_t *z, bool is_update) {
  if (!is_update) {
    // Note: Check if your header uses 'address1' or 'Address1' (C is case
    // sensitive)
    snprintf(
        payload_buffer, 1024,
        "{\"service_id\":\"%s\",\"location\":{\"address\":{\"line1\":\"%s\"}}}",
        o->monitor_service_id, o->address1);
  } else {
    snprintf(payload_buffer, 1024,
             "{\"event_type\":\"%s\",\"event_body\":{\"value\":\"%s\"}}",
             z->type, z->description);
  }
}

// CHANGE: struct config* -> config_t* |  struct zone* -> zone_t*
void payload_build_telegram(config_t *o, zone_t *z) {
  snprintf(payload_buffer, 1024, "{\"chat_id\":\"%s\",\"text\":\"Alarm: %s\"}",
           o->telegram_id, z->location);
}

// CHANGE: struct config* -> config_t* |  struct zone* -> zone_t* | struct
// contact* -> user_t*
void payload_build_smtp(config_t *o, zone_t *z, user_t *c) {
  (void)o;
  snprintf(payload_buffer, 1024, "To: %s\r\nSubject: Alert\r\n\r\n%s", c->phone,
           z->description);
}

bool payload_parse_response(int slot, const char *raw, char *out_id) {
  // Logic remains same
  if (slot == SLOT_NOONLIGHT) {
    char *p = strstr(raw, "\"id\":\"");
    if (p) {
      sscanf(p + 6, "%63[^\"]", out_id);
      return true;
    }
  } else if (slot == SLOT_TELEGRAM) {
    return strstr(raw, "\"ok\":true") != NULL;
  } else if (slot == SLOT_SMTP) {
    return strstr(raw, "250") != NULL;
  }
  return false;
}
