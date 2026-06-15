#ifndef LOGGING_H
#define LOGGING_H

/**
 * @file logging.h
 * @brief Configurable logging system with platform-specific output
 * 
 * Provides unified logging macros that automatically select:
 * - ESP32: Native ESP-IDF logging system
 * - Linux: ANSI colored console output for testing
 * 
 * Supports 5 log levels (DEBUG, INFO, WARNING, ERROR, NONE) that can be
 * changed at runtime to control verbosity in production vs development.
 */

#include <stdio.h>

/**
 * @enum log_level_t
 * @brief Logging verbosity levels
 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,    /**< Most verbose - all messages */
    LOG_LEVEL_INFO = 1,     /**< Information + warnings + errors */
    LOG_LEVEL_WARNING = 2,  /**< Warnings + errors only */
    LOG_LEVEL_ERROR = 3,    /**< Errors only */
    LOG_LEVEL_NONE = 4      /**< Silence all output */
} log_level_t;

// Global log level
extern log_level_t g_log_level;

/**
 * @brief Set global logging level at runtime
 * 
 * Changes verbosity of all LOG_* macros. On ESP32, also updates
 * the IDF logging level for consistency.
 * 
 * @param level New logging level to apply
 * 
 * @example
 * logging_set_level(LOG_LEVEL_DEBUG);  // Development: verbose
 * logging_set_level(LOG_LEVEL_ERROR);  // Production: errors only
 */
void logging_set_level(log_level_t level);

// Colored logging macros
#ifdef ESP_PLATFORM
    #include "esp_log.h"
    // On ESP32, use native logging system
    #define LOG_DEBUG(tag, fmt, ...) \
        if (g_log_level <= LOG_LEVEL_DEBUG) ESP_LOGD(tag, fmt, ##__VA_ARGS__)
    #define LOG_INFO(tag, fmt, ...) \
        if (g_log_level <= LOG_LEVEL_INFO) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
    #define LOG_WARN(tag, fmt, ...) \
        if (g_log_level <= LOG_LEVEL_WARNING) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
    #define LOG_ERROR(tag, fmt, ...) \
        if (g_log_level <= LOG_LEVEL_ERROR) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#else
    // On Linux, use simple printf-based logging with colors
    #define ANSI_COLOR_RED     "\x1b[31m"
    #define ANSI_COLOR_YELLOW  "\x1b[33m"
    #define ANSI_COLOR_GREEN   "\x1b[32m"
    #define ANSI_COLOR_BLUE    "\x1b[34m"
    #define ANSI_COLOR_RESET   "\x1b[0m"

    extern int mock_printf(const char *fmt, ...);

    #define LOG_DEBUG(tag, fmt, ...) \
        if (g_log_level <= LOG_LEVEL_DEBUG) mock_printf(ANSI_COLOR_BLUE   "[D][%s] " fmt "\n" ANSI_COLOR_RESET, tag, ##__VA_ARGS__)
    #define LOG_INFO(tag, fmt, ...) \
        if (g_log_level <= LOG_LEVEL_INFO) mock_printf(ANSI_COLOR_GREEN   "[I][%s] " fmt "\n" ANSI_COLOR_RESET, tag, ##__VA_ARGS__)
    #define LOG_WARN(tag, fmt, ...) \
        if (g_log_level <= LOG_LEVEL_WARNING) mock_printf(ANSI_COLOR_YELLOW "[W][%s] " fmt "\n" ANSI_COLOR_RESET, tag, ##__VA_ARGS__)
    #define LOG_ERROR(tag, fmt, ...) \
        if (g_log_level <= LOG_LEVEL_ERROR) mock_printf(ANSI_COLOR_RED    "[E][%s] " fmt "\n" ANSI_COLOR_RESET, tag, ##__VA_ARGS__)
#endif

#endif
