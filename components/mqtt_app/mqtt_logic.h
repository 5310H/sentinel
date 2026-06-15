#ifndef MQTT_LOGIC_H
#define MQTT_LOGIC_H

#include "mongoose.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {VS
#endif

// Public functions
void start_sentinel_mqtt(void);
void mqtt_publish_status(void);
void mqtt_publish_alert(const char *topic, const char *msg);

// Shared connection handle (externed so others can check if s_conn != NULL)
extern struct mg_connection *s_conn;

#ifdef __cplusplus
}
#endif

#endif // MQTT_LOGIC_H