#include "audit_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#ifdef ESP_PLATFORM
    #include "esp_log.h"
    #include "esp_spiffs.h"
    static const char *TAG = "AUDIT_LOG";
#else
    #define TAG "AUDIT_LOG"
    #define ESP_LOGI(tag, fmt, ...) printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__)
    #define ESP_LOGW(tag, fmt, ...) printf("[WARN][%s] " fmt "\n", tag, ##__VA_ARGS__)
    #define ESP_LOGE(tag, fmt, ...) printf("[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

// Circular buffer
static audit_entry_t log_entries[AUDIT_LOG_MAX_ENTRIES];
static uint32_t next_seq = 1;
static int write_index = 0;
static int entry_count = 0;
static bool initialized = false;

// Auto-save every N entries
#define AUDIT_LOG_AUTO_SAVE_INTERVAL 10
static int entries_since_save = 0;

#ifdef ESP_PLATFORM
    #define AUDIT_LOG_PATH "/spiffs/audit.log"
#else
    #define AUDIT_LOG_PATH "audit.log"
#endif

bool audit_log_init(void) {
    if (initialized) return true;
    
    memset(log_entries, 0, sizeof(log_entries));
    
    // Try to load existing log
    FILE *f = fopen(AUDIT_LOG_PATH, "r");
    if (f) {
        // Read header: next_seq, entry_count
        if (fscanf(f, "%" SCNu32 ",%d\n", &next_seq, &entry_count) == 2) {
            // Read entries (circular buffer, newest first)
            for (int i = 0; i < entry_count && i < AUDIT_LOG_MAX_ENTRIES; i++) {
                audit_entry_t *entry = &log_entries[i];
                if (fscanf(f, "%" SCNu32 ",%ld,%31[^,],%15[^,],%31[^,],%31[^,],%d,%127[^\n]\n",
                    &entry->seq, (long*)&entry->timestamp, entry->user, entry->ip_address,
                    entry->action, entry->resource, (int*)&entry->success, entry->details) != 8) {
                    break;
                }
            }
            write_index = entry_count % AUDIT_LOG_MAX_ENTRIES;
            ESP_LOGI(TAG, "Loaded %d audit log entries (seq=%u)", entry_count, next_seq);
        }
        fclose(f);
    } else {
        ESP_LOGI(TAG, "No existing audit log, starting fresh");
    }
    
    initialized = true;
    
    // Log system startup
    audit_log_write("system", "", "SYSTEM_BOOT", "system", true, "Audit log initialized");
    
    return true;
}

void audit_log_write(const char *user, const char *ip_address, const char *action, 
                     const char *resource, bool success, const char *details) {
    if (!initialized) {
        ESP_LOGW(TAG, "Audit log not initialized");
        return;
    }
    
    audit_entry_t *entry = &log_entries[write_index];
    
    entry->seq = next_seq++;
    entry->timestamp = time(NULL);
    
    strncpy(entry->user, user ? user : "unknown", AUDIT_LOG_USER_LEN - 1);
    entry->user[AUDIT_LOG_USER_LEN - 1] = '\0';
    
    strncpy(entry->ip_address, ip_address ? ip_address : "", AUDIT_LOG_IP_LEN - 1);
    entry->ip_address[AUDIT_LOG_IP_LEN - 1] = '\0';
    
    strncpy(entry->action, action ? action : "", AUDIT_LOG_ACTION_LEN - 1);
    entry->action[AUDIT_LOG_ACTION_LEN - 1] = '\0';
    
    strncpy(entry->resource, resource ? resource : "", AUDIT_LOG_ACTION_LEN - 1);
    entry->resource[AUDIT_LOG_ACTION_LEN - 1] = '\0';
    
    entry->success = success;
    
    strncpy(entry->details, details ? details : "", AUDIT_LOG_DETAILS_LEN - 1);
    entry->details[AUDIT_LOG_DETAILS_LEN - 1] = '\0';
    
    // Move to next slot (circular)
    write_index = (write_index + 1) % AUDIT_LOG_MAX_ENTRIES;
    if (entry_count < AUDIT_LOG_MAX_ENTRIES) {
        entry_count++;
    }
    
    // Auto-save periodically
    entries_since_save++;
    if (entries_since_save >= AUDIT_LOG_AUTO_SAVE_INTERVAL) {
        audit_log_flush();
        entries_since_save = 0;
    }
    
    // Log to console for immediate visibility
    ESP_LOGI(TAG, "[AUDIT] %s | %s | %s | %s | %s",
        entry->user, entry->action, entry->resource,
        success ? "SUCCESS" : "FAIL", entry->details);
}

int audit_log_get_entries(audit_entry_t *out_entries, int max_entries, uint32_t start_seq) {
    if (!initialized || !out_entries) return 0;
    
    int count = 0;
    
    // Return newest first (reverse order from circular buffer)
    int read_index = (write_index - 1 + AUDIT_LOG_MAX_ENTRIES) % AUDIT_LOG_MAX_ENTRIES;
    
    for (int i = 0; i < entry_count && count < max_entries; i++) {
        audit_entry_t *entry = &log_entries[read_index];
        
        // Filter by sequence number if requested
        if (start_seq > 0 && entry->seq < start_seq) {
            break;
        }
        
        memcpy(&out_entries[count], entry, sizeof(audit_entry_t));
        count++;
        
        read_index = (read_index - 1 + AUDIT_LOG_MAX_ENTRIES) % AUDIT_LOG_MAX_ENTRIES;
    }
    
    return count;
}

char *audit_log_to_json(char *out_json, size_t max_json_len, int max_entries) {
    if (!initialized || !out_json) return NULL;
    
    char *p = out_json;
    size_t remaining = max_json_len;
    
    int written = snprintf(p, remaining, "{\"seq\":%" PRIu32 ",\"entries\":[", next_seq - 1);
    if (written < 0 || (size_t)written >= remaining) return NULL;
    p += written;
    remaining -= written;
    
    audit_entry_t entries[100];
    int count = audit_log_get_entries(entries, max_entries < 100 ? max_entries : 100, 0);
    
    for (int i = 0; i < count; i++) {
        audit_entry_t *e = &entries[i];
        
        // Format timestamp as ISO 8601
        char time_str[32];
        struct tm *tm_info = localtime(&e->timestamp);
        strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", tm_info);
        
        written = snprintf(p, remaining,
            "%s{\"seq\":%" PRIu32 ",\"timestamp\":\"%s\",\"user\":\"%s\",\"ip\":\"%s\","
            "\"action\":\"%s\",\"resource\":\"%s\",\"success\":%s,\"details\":\"%s\"}",
            (i > 0 ? "," : ""), e->seq, time_str, e->user, e->ip_address,
            e->action, e->resource, e->success ? "true" : "false", e->details);
        
        if (written < 0 || (size_t)written >= remaining) {
            // Buffer full, truncate
            break;
        }
        p += written;
        remaining -= written;
    }
    
    written = snprintf(p, remaining, "]}");
    if (written < 0 || (size_t)written >= remaining) return NULL;
    
    return out_json;
}

void audit_log_flush(void) {
    if (!initialized) return;
    
    FILE *f = fopen(AUDIT_LOG_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open audit log for writing");
        return;
    }
    
    // Write header
    fprintf(f, "%" PRIu32 ",%d\n", next_seq, entry_count);
    
    // Write entries (newest first for easier retrieval)
    int read_index = (write_index - 1 + AUDIT_LOG_MAX_ENTRIES) % AUDIT_LOG_MAX_ENTRIES;
    for (int i = 0; i < entry_count; i++) {
        audit_entry_t *entry = &log_entries[read_index];
        fprintf(f, "%" PRIu32 ",%ld,%s,%s,%s,%s,%d,%s\n",
            entry->seq, (long)entry->timestamp, entry->user, entry->ip_address,
            entry->action, entry->resource, entry->success, entry->details);
        read_index = (read_index - 1 + AUDIT_LOG_MAX_ENTRIES) % AUDIT_LOG_MAX_ENTRIES;
    }
    
    fclose(f);
    ESP_LOGI(TAG, "Flushed %d entries to disk", entry_count);
}

uint32_t audit_log_get_seq(void) {
    return next_seq;
}
