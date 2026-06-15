#ifndef AUDIT_LOG_H
#define AUDIT_LOG_H

/**
 * @file audit_log.h
 * @brief Security audit logging for forensic analysis
 * 
 * Records all security-relevant actions for compliance and investigation.
 * Circular buffer implementation with persistence to SPIFFS.
 * 
 * Logged Events:
 * - Authentication (success/failure)
 * - Arm/disarm operations
 * - User management (add/delete/modify)
 * - Configuration changes
 * - Zone bypass
 * - API access
 * - Firmware updates
 * 
 * Log Format: JSON array with timestamp, user, action, result, details
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define AUDIT_LOG_MAX_ENTRIES 100
#define AUDIT_LOG_USER_LEN 32
#define AUDIT_LOG_IP_LEN 16
#define AUDIT_LOG_ACTION_LEN 32
#define AUDIT_LOG_DETAILS_LEN 128

/**
 * @brief Audit log entry structure
 */
typedef struct {
    uint32_t seq;                              // Sequence number (monotonic)
    time_t timestamp;                          // Unix timestamp
    char user[AUDIT_LOG_USER_LEN];             // Username (or "system")
    char ip_address[AUDIT_LOG_IP_LEN];         // Client IP
    char action[AUDIT_LOG_ACTION_LEN];         // Action type
    char resource[AUDIT_LOG_ACTION_LEN];       // Resource affected (e.g., "/api/arm")
    bool success;                              // true = success, false = failure
    char details[AUDIT_LOG_DETAILS_LEN];       // Additional context
} audit_entry_t;

/**
 * @brief Initialize audit log system
 * 
 * Loads existing log from SPIFFS if available.
 * Creates /spiffs/audit.log if not exists.
 * 
 * @return true on success, false on error
 */
bool audit_log_init(void);

/**
 * @brief Write audit log entry
 * 
 * Appends entry to circular buffer and persists to SPIFFS.
 * Automatically wraps around when buffer full.
 * 
 * @param user Username performing action (or "system" for automated)
 * @param ip_address Client IP address (can be NULL)
 * @param action Action type (e.g., "LOGIN", "ARM_AWAY", "USER_ADD")
 * @param resource Resource affected (e.g., "/api/arm", "user:john_doe")
 * @param success true if action succeeded, false if failed
 * @param details Additional context (e.g., failure reason)
 * 
 * @example
 * audit_log_write("john_doe", "192.168.1.100", "LOGIN", "/api/login", true, "PIN authentication");
 * audit_log_write("jane_doe", "192.168.1.101", "ARM_AWAY", "/api/arm", true, "Armed via web UI");
 * audit_log_write("unknown", "192.168.1.200", "LOGIN", "/api/login", false, "Invalid PIN");
 */
void audit_log_write(const char *user, const char *ip_address, const char *action, 
                     const char *resource, bool success, const char *details);

/**
 * @brief Retrieve audit log entries
 * 
 * Returns entries from circular buffer, newest first.
 * 
 * @param out_entries Output array (min 'max_entries' elements)
 * @param max_entries Maximum entries to retrieve
 * @param start_seq Start from this sequence number (0 = most recent)
 * @return Number of entries copied
 * 
 * @example
 * audit_entry_t entries[100];
 * int count = audit_log_get_entries(entries, 100, 0);
 * for (int i = 0; i < count; i++) {
 *     printf("%s: %s %s\n", entries[i].user, entries[i].action, 
 *            entries[i].success ? "SUCCESS" : "FAIL");
 * }
 */
int audit_log_get_entries(audit_entry_t *out_entries, int max_entries, uint32_t start_seq);

/**
 * @brief Get entries as JSON string (for web API)
 * 
 * @param out_json Output buffer (min 4096 bytes recommended)
 * @param max_json_len Size of output buffer
 * @param max_entries Maximum entries to include
 * @return Pointer to out_json, or NULL on error
 */
char *audit_log_to_json(char *out_json, size_t max_json_len, int max_entries);

/**
 * @brief Flush log to SPIFFS
 * 
 * Forces immediate write to disk. Normally auto-saved every 10 entries.
 */
void audit_log_flush(void);

/**
 * @brief Get current sequence number
 * 
 * @return Next sequence number to be assigned
 */
uint32_t audit_log_get_seq(void);

#endif // AUDIT_LOG_H
