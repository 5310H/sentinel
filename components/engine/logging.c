#include "logging.h"

// Global log level (default to INFO)
log_level_t g_log_level = LOG_LEVEL_INFO;

void logging_set_level(log_level_t level) {
    if (level >= LOG_LEVEL_DEBUG && level <= LOG_LEVEL_NONE) {
        g_log_level = level;
    }

    #ifdef ESP_PLATFORM
        if (level <= LOG_LEVEL_NONE) {
            esp_log_level_set("*", ESP_LOG_NONE);
            if (level == LOG_LEVEL_DEBUG) esp_log_level_set("*", ESP_LOG_DEBUG);
            else if (level == LOG_LEVEL_INFO) esp_log_level_set("*", ESP_LOG_INFO);
            else if (level == LOG_LEVEL_WARNING) esp_log_level_set("*", ESP_LOG_WARN);
            else if (level == LOG_LEVEL_ERROR) esp_log_level_set("*", ESP_LOG_ERROR);
        }
    #endif
}
