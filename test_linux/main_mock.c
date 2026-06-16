#include "../components/esphome_api/esphome_api_client.h"
#include "audit_log.h"
#include "camera_mgr.h"
#include "engine.h"
#include "mongoose.h"
#include "mqtt_logic.h"
#include "noonlight.h"
#include "otp.h"
#include "session_mgr.h"
#include "storage_mgr.h"
#include "system_monitoring.h"
#include "tuya_mock.h"
#include "user_mgr.h"
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

// --- LOGGING BUFFER ---
#define MAX_LOG_LINES 50
#define MAX_LOG_LEN 256
char g_log_buf[MAX_LOG_LINES][MAX_LOG_LEN];
int g_log_idx = 0;
uint32_t g_log_seq = 0;
uint32_t g_log_seqs[MAX_LOG_LINES];

int mock_printf(const char *fmt, ...) {
  va_list args;

  // 1. Console output (keep original behavior)
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);

  // 2. Buffer output (for Web UI)
  va_start(args, fmt);
  char buf[MAX_LOG_LEN];
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  // Strip trailing newline for cleaner JSON
  int len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n')
    buf[len - 1] = '\0';

  strncpy(g_log_buf[g_log_idx], buf, MAX_LOG_LEN);
  g_log_seqs[g_log_idx] = ++g_log_seq;
  g_log_idx = (g_log_idx + 1) % MAX_LOG_LINES;
  return 0;
}
#define printf mock_printf

// --- 1. PROTOTYPES & EXTERNS ---
void monitor_init(void);
void mqtt_publish_status(void);
void start_sentinel_mqtt(void);
extern bool is_ready(void);
extern const char *engine_get_violation_name(void);
extern const char *engine_get_violation_type(void);
char *users_to_json(void);
void engine_handle_keypad_input(const char *pin);
void storage_save_network(void);

// Integration globals
// --- 1. PROTOTYPES & EXTERNS ---
int current_arm_state = 0;
int current_alarm_status = 0;
extern struct mg_mgr mgr;

int mock_gpio_pins[100] = {[0 ... 99] = 1};
bool mock_relay_states[MAX_RELAYS] = {false};
bool mock_esphome_relay_states[24] = {false}; // ESPHome relays 8-31
char mock_last_rf[32] = "None";

// --- 2. HELPER FUNCTIONS ---
int digitalRead(int pin) {
  if (pin < 0 || pin >= 100)
    return 1;
  return mock_gpio_pins[pin];
}
// This replaces get_json_param and satisfies the get_json_str calls
void get_json_str(struct mg_str body, const char *path, char *dst,
                  int dst_len) {
  // Mongoose 7.x uses mg_json_get_str which returns a heap-allocated string
  char *val = mg_json_get_str(body, path);
  if (val != NULL) {
    snprintf(dst, dst_len, "%s", val);
    free(val); // Critical: mg_json_get_str allocation must be freed
  } else {
    // Fallback: If the client sent the PIN as a raw JSON integer
    double num = 0;
    if (mg_json_get_num(body, path, &num)) {
      snprintf(dst, dst_len, "%.0f", num);
    } else {
      dst[0] = '\0';
    }
  }
}
// --- 3. GLUE LOGIC ---
// 1. Rename the function to __wrap_...
keypad_result_t __wrap_engine_check_keypad(const char *pin) {
  keypad_result_t res = {false, false, "None"};

  printf("\n--- KEYPAD VALIDATION START ---\n");
  printf("INPUT PIN: [%s]\n", pin);

  if (storage_get_config()->pin[0] == '\0') {
    printf("MASTER PIN: [EMPTY/NULL] - Check config.json formatting!\n");
  } else {
    printf("MASTER PIN: [%s]\n", storage_get_config()->pin);
  }

  fflush(stdout);

  // 1. Check Master PIN (9999)
  if (storage_get_config()->pin[0] != '\0') {
    if (strcmp(pin, storage_get_config()->pin) == 0) {
      printf("RESULT: MATCHED MASTER ADMIN\n");
      res.authenticated = true;
      res.is_admin = true;
      strncpy(res.name, storage_get_config()->name, sizeof(res.name) - 1);
      res.name[sizeof(res.name) - 1] = '\0';
      return res;
    }
  }

  printf("Checking against Secondary Users Database (Hashes)...\n");

  // 2. Check Database users (using centralized authentication)
  user_t *user = NULL;
  if (user_authenticate_pin(pin, &user) && user != NULL) {
    printf("RESULT: MATCHED USER [%s] (plaintext PIN)\n", user->name);
    res.authenticated = true;
    res.is_admin = user->is_admin;
    strncpy(res.name, user->name, sizeof(res.name) - 1);
    res.name[sizeof(res.name) - 1] = '\0';
    return res;
  }

  printf("DEBUG: Authentication failed for PIN [%s]. Loaded users:\n", pin);
  for (int i = 0; i < storage_get_user_count(); i++) {
    user_t *u = storage_get_user(i);
    if (u && strlen(u->name) > 0) {
      printf(" -> User '%s' | Admin: %s\n", u->name,
             u->is_admin ? "YES" : "NO");
    }
  }

  printf("RESULT: NO MATCH FOUND\n");
  return res;
}
// MOCK HAL WRAPPERS ---
void __wrap_hal_set_relay(int gpio, bool active) {
  int relay_idx = -1;
  for (int i = 0; i < storage_get_relay_count(); i++) {
    if (storage_get_relay(i)->gpio == gpio) {
      relay_idx = i;
      break;
    }
  }
  if (relay_idx >= 0 && relay_idx < MAX_RELAYS) {
    mock_relay_states[relay_idx] = active;
    printf("\n[HAL] Relay %d (GPIO %d) -> %s\n", relay_idx, gpio,
           active ? "ON" : "OFF");
  } else {
    printf("\n[HAL] Unknown Relay GPIO %d -> %s\n", gpio,
           active ? "ON" : "OFF");
  }
}
bool __wrap_hal_get_relay_state(int gpio) {
  int relay_idx = -1;
  for (int i = 0; i < storage_get_relay_count(); i++) {
    if (storage_get_relay(i)->gpio == gpio) {
      relay_idx = i;
      break;
    }
  }
  return (relay_idx >= 0 && relay_idx < MAX_RELAYS)
             ? mock_relay_states[relay_idx]
             : false;
}
void __wrap_hal_set_siren(bool active) {
  printf("\n[HAL] SIREN -> %s\n", active ? "ON" : "OFF");
}
void __wrap_hal_set_strobe(bool active) {
  printf("\n[HAL] STROBE -> %s\n", active ? "ON" : "OFF");
}
int __wrap_hal_get_zone_state(int zone_id) { return digitalRead(zone_id); }

// --- SECTION 3: THE HANDLER ---
void engine_handle_keypad_input(const char *pin) {
  // We call the 'real' name here; the linker uses the WRAPS to
  // redirect this call to __wrap_engine_check_keypad above.
  keypad_result_t res = engine_check_keypad(pin);

  if (res.authenticated) {
    printf("\n[AUTH] SUCCESS: Welcome %s!\n", res.name);
    if (engine_get_arm_state() == 0) {
      engine_ui_arm(1); // ARM_AWAY
    } else {
      engine_ui_disarm();
    }
  } else {
    printf("\n[AUTH] FAILED: Invalid PIN [%s]\n", pin);
  }
}
// --- ESPHome Connection Reject Handler ---
void esphome_reject_handler(struct mg_connection *c, int ev, void *ev_data) {
  (void)ev_data; // Suppress unused parameter warning

  if (ev == MG_EV_ACCEPT) {
    // Connection accepted - log it
    printf("[WARNING] ESPHome connection attempt from %s (device misconfigured "
           "- should connect TO us, not FROM us)\n",
           c->rem.is_ip6 ? "[IPv6]" : "[IPv4]");
  } else if (ev == MG_EV_READ && c->recv.len > 0) {
    // Data received - reject the connection
    printf("[WARNING] Rejecting ESPHome protocol data from misconfigured "
           "device\n");
    c->is_closing = 1;
  } else if (ev == MG_EV_CLOSE) {
    // Connection closed
    printf("[INFO] ESPHome connection closed\n");
  }
}
void web_handler(struct mg_connection *c, int ev, void *ev_data) {
  // Handle TLS initialization for HTTPS connections
  if (ev == MG_EV_ACCEPT && c->fn_data != NULL) {
    // fn_data is non-NULL for HTTPS connections (see listener setup)
    struct mg_tls_opts opts = {
        .cert =
            mg_str("./server.pem"), // Certificate file path for Linux testing
        .key = mg_str(
            "./server.pem"), // Key file (same file contains both cert and key)
    };
    mg_tls_init(c, &opts);
  }
  // Handle ESPHome protocol connections (reject them gracefully)
  else if (ev == MG_EV_READ) {
    // Check if this looks like an ESPHome connection (not HTTP)
    // ESPHome devices may be misconfigured to connect to our server as their
    // API endpoint, but we should be connecting TO them, not receiving
    // connections FROM them. Reject non-HTTP connections to prevent socket
    // errors.
    if (c->recv.len > 0) {
      char *data = (char *)c->recv.buf;
      // Check if the first few bytes look like HTTP methods
      if (c->recv.len >= 3 && strncmp(data, "GET", 3) != 0 &&
          strncmp(data, "POST", 4) != 0 && strncmp(data, "PUT", 3) != 0 &&
          strncmp(data, "DELETE", 6) != 0 && strncmp(data, "HEAD", 4) != 0 &&
          strncmp(data, "OPTIONS", 7) != 0 &&
          strncmp(data, "CONNECT", 7) != 0) {
        // This looks like a non-HTTP connection, likely ESPHome protocol
        printf("[WARNING] Rejecting non-HTTP connection (likely ESPHome "
               "protocol - device should connect TO us, not FROM us)\n");
        c->is_closing = 1; // Close the connection
        return;
      }
    }
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;

    // 1. USER MGMT
    if (mg_strcmp(hm->uri, mg_str("/api/users")) == 0) {
      char *json = users_to_json();
      mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json);
      free(json);
    } else if (mg_strcmp(hm->uri, mg_str("/api/users/add")) == 0) {
      char name[STR_SMALL] = {0}, pin[STR_SMALL] = {0}, email[STR_MEDIUM] = {0},
           n_buf[10] = {0}, emergency_pin[STR_SMALL] = {0};
      get_json_str(hm->body, "$.name", name, sizeof(name));
      get_json_str(hm->body, "$.pin", pin, sizeof(pin));
      get_json_str(hm->body, "$.email", email, sizeof(email));
      get_json_str(hm->body, "$.notify", n_buf, sizeof(n_buf));
      get_json_str(hm->body, "$.emergency_pin", emergency_pin,
                   sizeof(emergency_pin));
      bool is_admin = false;
      mg_json_get_bool(hm->body, "$.is_admin", &is_admin);
      user_add(name, pin, "", email, atoi(n_buf), is_admin, emergency_pin);
      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\"}");
    } else if (mg_strcmp(hm->uri, mg_str("/api/users/update")) == 0) {
      char name[STR_SMALL] = {0}, pin[STR_SMALL] = {0}, email[STR_MEDIUM] = {0},
           n_buf[10] = {0}, emergency_pin[STR_SMALL] = {0};
      get_json_str(hm->body, "$.name", name, sizeof(name));
      get_json_str(hm->body, "$.pin", pin, sizeof(pin));
      get_json_str(hm->body, "$.email", email, sizeof(email));
      get_json_str(hm->body, "$.notify", n_buf, sizeof(n_buf));
      get_json_str(hm->body, "$.emergency_pin", emergency_pin,
                   sizeof(emergency_pin));
      bool is_admin = false;
      mg_json_get_bool(hm->body, "$.is_admin", &is_admin);
      user_update(name, pin, "", email, atoi(n_buf), is_admin ? 1 : 0,
                  emergency_pin);
      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\"}");
    } else if (mg_strcmp(hm->uri, mg_str("/api/users/delete")) == 0) {
      char name[STR_SMALL] = {0};
      get_json_str(hm->body, "$.name", name, sizeof(name));
      user_drop(name);
      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\"}");
    } else if (mg_strcmp(hm->uri, mg_str("/api/users/set-totp")) == 0) {
      char name[STR_SMALL] = {0};
      char totp_secret[STR_MEDIUM] = {0};
      get_json_str(hm->body, "$.name", name, sizeof(name));
      get_json_str(hm->body, "$.totp_secret", totp_secret, sizeof(totp_secret));

      if (strlen(name) > 0) {
        bool success = user_set_totp_secret(name, totp_secret);
        if (success) {
          mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"status\":\"ok\"}");
        } else {
          mg_http_reply(
              c, 404, "Content-Type: application/json\r\n",
              "{\"status\":\"error\",\"message\":\"User not found\"}");
        }
      } else {
        mg_http_reply(
            c, 400, "Content-Type: application/json\r\n",
            "{\"status\":\"error\",\"message\":\"Missing user name\"}");
      }
    } else if (mg_strcmp(hm->uri, mg_str("/api/users/send-totp-email")) == 0) {
      // Send 2FA setup secret via email
      char name[STR_SMALL] = {0};
      char totp_secret[STR_MEDIUM] = {0};
      get_json_str(hm->body, "$.name", name, sizeof(name));
      get_json_str(hm->body, "$.totp_secret", totp_secret, sizeof(totp_secret));

      if (strlen(name) > 0 && strlen(totp_secret) > 0) {
        const char *user_email = NULL;

        // Check if this is the storage_get_config()->pin user (name matches
        // storage_get_config()->name)
        if (strcmp(name, storage_get_config()->name) == 0) {
          user_email = storage_get_config()->email;
        } else {
          // Look up in users array
          for (int i = 0; i < storage_get_user_count(); i++) {
            if (strcmp(storage_get_user(i)->name, name) == 0) {
              user_email = storage_get_user(i)->email;
              break;
            }
          }
        }

        if (user_email && strlen(user_email) > 0) {
          // Format email body
          char email_body[512];
          snprintf(
              email_body, sizeof(email_body),
              "2FA Setup Code for Sentinel Alarm System\n\n"
              "Your setup code is:\n%s\n\n"
              "Enter this code in your authenticator app to complete setup.\n"
              "Account: %s\n"
              "User: %s\n\n"
              "If you did not request this, please ignore this email.",
              totp_secret, storage_get_config()->account_id, name);

          // Send email (mock: just log it)
          printf("[MOCK EMAIL] To: %s | Subject: 2FA Setup Code | Body:\n%s\n",
                 user_email, email_body);

          mg_http_reply(
              c, 200, "Content-Type: application/json\r\n",
              "{\"status\":\"ok\",\"message\":\"Setup code sent to %s\"}",
              user_email);
        } else {
          mg_http_reply(
              c, 404, "Content-Type: application/json\r\n",
              "{\"status\":\"error\",\"message\":\"User email not found\"}");
        }
      } else {
        mg_http_reply(
            c, 400, "Content-Type: application/json\r\n",
            "{\"status\":\"error\",\"message\":\"Missing name or secret\"}");
      }
    }

    // 2. AUTH & CONTROL (2FA Support)
    else if (mg_strcmp(hm->uri, mg_str("/api/auth")) == 0) {
      char pin_in[STR_SMALL] = {0};
      char totp_in[STR_SMALL] = {0};
      char step_buf[16] = {0};

      get_json_str(hm->body, "$.pin", pin_in, sizeof(pin_in));
      get_json_str(hm->body, "$.totp", totp_in, sizeof(totp_in));
      get_json_str(hm->body, "$.step", step_buf, sizeof(step_buf));

      printf("\n[AUTH API] Login attempt from Web UI. Extracted PIN: [%s] "
             "(Length: %zu)\n",
             pin_in, strlen(pin_in));

      keypad_result_t res = engine_check_keypad(pin_in);

      // Step 1: PIN validation only
      if (strcmp(step_buf, "pin") == 0) {
        if (res.authenticated) {
          // Master PIN (via config) does not require 2FA
          bool requires_totp = false;

          if (!res.is_admin ||
              strcmp(res.name, storage_get_config()->name) != 0) {
            // Regular user - check if 2FA is configured
            const char *user_secret = engine_get_user_totp_secret(res.name);
            requires_totp = (user_secret != NULL && strlen(user_secret) > 0);
          }

          // Don't send secret to client - security risk
          // Client should use session-based approach or generate code
          // client-side Master PIN skips 2FA, regular users require it if
          // configured
          mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"authenticated\":true,\"is_admin\":%s,\"name\":\"%"
                        "s\",\"state\":%d,\"requires_totp\":%s}",
                        res.is_admin ? "true" : "false", res.name,
                        engine_get_arm_state(),
                        requires_totp ? "true" : "false");
        } else {
          mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"authenticated\":false,\"is_admin\":false,\"name\":"
                        "\"Unknown\",\"state\":%d}",
                        engine_get_arm_state());
        }
      }
      // Step 2: TOTP validation (PIN + TOTP)
      else if (strcmp(step_buf, "totp") == 0 && totp_in[0] != '\0') {
        if (!res.authenticated) {
          // PIN invalid
          mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"authenticated\":false,\"is_admin\":false,\"name\":"
                        "\"Unknown\",\"state\":%d}",
                        engine_get_arm_state());
        } else {
          // PIN is valid, now validate TOTP
          bool totp_valid = false;

          // Get user's TOTP secret (per-user, not default)
          const char *user_secret = engine_get_user_totp_secret(res.name);

          // If user has no TOTP secret set, allow PIN-only for now
          if (!user_secret || strlen(user_secret) == 0) {
            // User doesn't have 2FA configured - allow PIN-only
            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                          "{\"authenticated\":true,\"is_admin\":%s,\"name\":\"%"
                          "s\",\"state\":%d}",
                          res.is_admin ? "true" : "false", res.name,
                          engine_get_arm_state());
          } else {
            // User has TOTP secret configured - validate TOTP code
            // Validate TOTP code with ±1 time window tolerance for clock skew
            if (strlen(totp_in) == 6 && user_secret) {
              totp_valid = validate_totp(user_secret, totp_in, 30, 6, 1);
            }

            if (totp_valid) {
              mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                            "{\"authenticated\":true,\"is_admin\":%s,\"name\":"
                            "\"%s\",\"state\":%d}",
                            res.is_admin ? "true" : "false", res.name,
                            engine_get_arm_state());
            } else {
              mg_http_reply(
                  c, 200, "Content-Type: application/json\r\n",
                  "{\"authenticated\":false,\"is_admin\":false,\"name\":"
                  "\"Unknown\",\"state\":%d,\"error\":\"Invalid TOTP code\"}",
                  engine_get_arm_state());
            }
          }
        }
      }
      // Legacy: PIN-only authentication (for backward compatibility)
      else {
        // Original behavior - PIN only (no 2FA)
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"authenticated\":%s,\"is_admin\":%s,\"name\":\"%s\","
                      "\"state\":%d}",
                      res.authenticated ? "true" : "false",
                      res.is_admin ? "true" : "false", res.name,
                      engine_get_arm_state());
      }
    }
    // 3. API: Full Status (Matches index.html and maintenance.html
    // expectations)
    else if (mg_strcmp(hm->uri, mg_str("/api/status")) == 0) {
      static char s_buf[45056];
      memset(s_buf, 0, sizeof(s_buf));

      int up_sec = (int)(mg_now() / 1000);
      char up_str[32];
      snprintf(up_str, sizeof(up_str), "%dh %dm", up_sec / 3600,
               (up_sec % 3600) / 60);

      // Console Debugging
      //            printf("[DEBUG] Web UI Status Request: %d Zones, %d Relays
      //            detected\n", storage_get_zone_count(),
      //            storage_get_relay_count());

      // 1. Build Config Object
      int off = snprintf(
          s_buf, sizeof(s_buf),
          "{\"config\":{"
          "\"acct_id\":\"%s\",\"pin\":\"%s\",\"name\":\"%s\",\"addr1\":\"%s\","
          "\"addr2\":\"%s\","
          "\"city\":\"%s\",\"state_prov\":\"%s\",\"zip\":\"%s\",\"email\":\"%"
          "s\",\"phone\":\"%s\","
          "\"instr\":\"%s\",\"lat\":%f,\"lon\":%f,\"acc\":%d,"
          "\"mon_key\":\"%s\",\"mon_url\":\"%s\",\"notify\":%d,"
          "\"fire\":%s,\"police\":%s,\"med\":%s,\"oth\":%s,"
          "\"smtp_srv\":\"%s\",\"smtp_port\":%d,\"smtp_user\":\"%s\",\"smtp_"
          "pass\":\"%s\","
          "\"mqtt_srv\":\"%s\",\"mqtt_port\":%d,\"mqtt_user\":\"%s\",\"mqtt_"
          "pass\":\"%s\","
          "\"tg_id\":\"%s\",\"tg_tok\":\"%s\",\"tg_en\":%s,\"nvr_url\":\"%s\","
          "\"ha_url\":\"%s\","
          "\"ent_d\":%d,\"ext_d\":%d,\"can_d\":%d,\"state\":%d,\"ready\":%s,"
          "\"violation\":\"%s\",\"violation_type\":\"%s\",\"uptime\":\"%s\","
          "\"mac\":\"%s\"},",
          storage_get_config()->account_id, storage_get_config()->pin,
          storage_get_config()->name, storage_get_config()->address1,
          storage_get_config()->address2, storage_get_config()->city,
          storage_get_config()->state, storage_get_config()->zip_code,
          storage_get_config()->email, storage_get_config()->phone,
          storage_get_config()->instructions, storage_get_config()->latitude,
          storage_get_config()->longitude, storage_get_config()->accuracy,
          storage_get_config()->monitor_service_key,
          storage_get_config()->monitoring_url, storage_get_config()->notify,
          storage_get_config()->is_monitor_fire ? "true" : "false",
          storage_get_config()->is_monitor_police ? "true" : "false",
          storage_get_config()->is_monitor_medical ? "true" : "false",
          storage_get_config()->is_monitor_other ? "true" : "false",
          storage_get_config()->smtp_server, storage_get_config()->smtp_port,
          storage_get_config()->smtp_user, storage_get_config()->smtp_pass,
          storage_get_config()->mqtt_server, storage_get_config()->mqtt_port,
          storage_get_config()->mqtt_user, storage_get_config()->mqtt_pass,
          storage_get_config()->telegram_id,
          storage_get_config()->telegram_token,
          storage_get_config()->is_telegram_enabled ? "true" : "false",
          storage_get_config()->nvrserver_url,
          storage_get_config()->haintegration_url,
          storage_get_config()->entry_delay, storage_get_config()->exit_delay,
          storage_get_config()->cancel_delay, engine_get_arm_state(),
          is_ready() ? "true" : "false", engine_get_violation_name(),
          engine_get_violation_type(), up_str, "AC:DC:12:34:56:78");

      off +=
          snprintf(s_buf + off, sizeof(s_buf) - off,
                   "\"network\":{\"use_dhcp\":%s,\"ip\":\"%s\",\"netmask\":\"%"
                   "s\",\"gateway\":\"%s\",\"dns\":\"%s\"},",
                   storage_get_network()->use_dhcp ? "true" : "false",
                   storage_get_network()->ip, storage_get_network()->netmask,
                   storage_get_network()->gateway, storage_get_network()->dns);
      off += snprintf(s_buf + off, sizeof(s_buf) - off,
                      "\"rf\":{\"last\":\"%s\"},", mock_last_rf);

      // 2. Append Zones Array (Required for maintenance.html)
      off += snprintf(s_buf + off, sizeof(s_buf) - off, "\"zones\":[");

      // Physical zones (1-32)
      for (int i = 0; i < storage_get_zone_count(); i++) {
        if (storage_get_zone(i)->gpio >= 0) { // Only physical zones with GPIO
          // In digitalRead, 0 usually means "Closed/Short" (Violated for NC
          // sensors)
          bool is_open = (digitalRead(storage_get_zone(i)->gpio) == 0);
          off += snprintf(
              s_buf + off, sizeof(s_buf) - off,
              "{\"id\":%d,\"name\":\"%s\",\"description\":\"%s\",\"location\":"
              "\"%s\",\"open\":%s,\"violated\":%s},",
              storage_get_zone(i)->id, storage_get_zone(i)->name,
              storage_get_zone(i)->description, storage_get_zone(i)->location,
              is_open ? "true" : "false", is_open ? "true" : "false");
        }
      }

      // ESPHome virtual zones (33-64)
      for (int i = 0; i < storage_get_esphome_count(); i++) {
        if (storage_get_esphome_device(i)->virtual_zone_start > 0 &&
            storage_get_esphome_device(i)->enabled) {
          int zone_id = storage_get_esphome_device(i)->virtual_zone_start;
          const char *desc =
              storage_get_esphome_device(i)->description[0] != '\0'
                  ? storage_get_esphome_device(i)->description
                  : storage_get_esphome_device(i)->friendly_name;
          off += snprintf(
              s_buf + off, sizeof(s_buf) - off,
              "{\"id\":%d,\"name\":\"%s\",\"description\":\"%s\",\"location\":"
              "\"%s\",\"open\":false,\"violated\":false},",
              zone_id, storage_get_esphome_device(i)->friendly_name, desc,
              storage_get_esphome_device(i)->location);
        }
      }

      // Tuya virtual zones (67-96)
      tuya_device_t tuya_devs[TUYA_DEVICE_MAX_COUNT];
      int tuya_count = 0;
      if (tuya_mgr_get_devices(tuya_devs, TUYA_DEVICE_MAX_COUNT, &tuya_count) ==
          ESP_OK) {
        for (int i = 0; i < tuya_count; i++) {
          if (tuya_devs[i].virtual_zone_id > 0 && tuya_devs[i].enabled) {
            bool is_open = false;
            // Door/window sensors use contact_open
            if (tuya_devs[i].type == TUYA_DEV_TYPE_DOOR_SENSOR) {
              is_open = tuya_devs[i].contact_open;
            }
            // Motion sensors use motion_detected
            else if (tuya_devs[i].type == TUYA_DEV_TYPE_MOTION_SENSOR) {
              is_open = tuya_devs[i].motion_detected;
            }

            const char *desc = tuya_devs[i].description[0] != '\0'
                                   ? tuya_devs[i].description
                                   : tuya_devs[i].name;
            off +=
                snprintf(s_buf + off, sizeof(s_buf) - off,
                         "{\"id\":%d,\"name\":\"%s\",\"description\":\"%s\","
                         "\"location\":\"%s\",\"open\":%s,\"violated\":%s},",
                         tuya_devs[i].virtual_zone_id, tuya_devs[i].name, desc,
                         tuya_devs[i].location, is_open ? "true" : "false",
                         is_open ? "true" : "false");
          }
        }
      }

      // Remove trailing comma if zones exist
      if (off > 0 && s_buf[off - 1] == ',') {
        off--;
      }

      off += snprintf(s_buf + off, sizeof(s_buf) - off, "],");

      // 3. Append Relays Array (Required for maintenance.html)
      off += snprintf(s_buf + off, sizeof(s_buf) - off, "\"relays\":[");

      // Physical relays (1-8)
      for (int i = 0; i < storage_get_relay_count(); i++) {
        if (storage_get_relay(i)->id <= 8) { // Only physical relays
          off += snprintf(
              s_buf + off, sizeof(s_buf) - off,
              "{\"id\":%d,\"name\":\"%s\",\"location\":\"%s\",\"active\":%s},",
              storage_get_relay(i)->id, storage_get_relay(i)->name,
              storage_get_relay(i)->location,
              mock_relay_states[i] ? "true" : "false");
        }
      }

      // ESPHome virtual relays (8-31)
      for (int i = 0; i < storage_get_esphome_count(); i++) {
        if (storage_get_esphome_device(i)->virtual_relay_start > 0 &&
            storage_get_esphome_device(i)->enabled) {
          int relay_id = storage_get_esphome_device(i)->virtual_relay_start;
          int esphome_relay_idx = relay_id - 8;
          bool active = (esphome_relay_idx >= 0 && esphome_relay_idx < 24)
                            ? mock_esphome_relay_states[esphome_relay_idx]
                            : false;
          off += snprintf(
              s_buf + off, sizeof(s_buf) - off,
              "{\"id\":%d,\"name\":\"%s\",\"location\":\"%s\",\"active\":%s},",
              relay_id, storage_get_esphome_device(i)->friendly_name,
              storage_get_esphome_device(i)->location,
              active ? "true" : "false");
        }
      }

      // Tuya virtual relays (32-63)
      tuya_device_t tuya_relay_devs[TUYA_DEVICE_MAX_COUNT];
      int tuya_relay_count = 0;
      if (tuya_mgr_get_devices(tuya_relay_devs, TUYA_DEVICE_MAX_COUNT,
                               &tuya_relay_count) == ESP_OK) {
        for (int i = 0; i < tuya_relay_count; i++) {
          if (tuya_relay_devs[i].virtual_relay_id > 0 &&
              tuya_relay_devs[i].enabled) {
            off +=
                snprintf(s_buf + off, sizeof(s_buf) - off,
                         "{\"id\":%d,\"name\":\"%s\",\"location\":\"%s\","
                         "\"active\":%s},",
                         tuya_relay_devs[i].virtual_relay_id,
                         tuya_relay_devs[i].name, tuya_relay_devs[i].location,
                         tuya_relay_devs[i].switch_on ? "true" : "false");
          }
        }
      }

      // Remove trailing comma if relays exist
      if (off > 0 && s_buf[off - 1] == ',') {
        off--;
      }

      off += snprintf(s_buf + off, sizeof(s_buf) - off,
                      "]}"); // Final Closing Brace

      mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", s_buf);
    }
    // 4. API: System Status (for index.html temperature/power/tamper display)
    else if (mg_strcmp(hm->uri, mg_str("/status")) == 0) {
      system_status_t status = {0};
      system_monitoring_update(&status);
      int temp_f = (status.temperature_c * 9 / 5) + 32; // Convert to Fahrenheit
      char status_buf[256];
      snprintf(status_buf, sizeof(status_buf),
               "{\"temperature_c\":%d,\"temperature_f\":%d,\"on_backup_power\":"
               "%s,\"tamper_detected\":%s}",
               status.temperature_c, temp_f,
               status.on_backup_power ? "true" : "false",
               status.tamper_detected ? "true" : "false");
      mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s",
                    status_buf);
    }
    // 5. API: Diagnostics
    else if (mg_strcmp(hm->uri, mg_str("/diagnostics")) == 0) {
      char *json_str = NULL;
      if (system_monitoring_get_diagnostics_json(&json_str) == 0 && json_str) {
        mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s",
                      json_str);
        free(json_str);
      } else {
        mg_http_reply(c, 500, "", "{\"error\":\"fail\"}");
      }
    }
    // 13. API: Logs
    else if (mg_strcmp(hm->uri, mg_str("/api/logs")) == 0) {
      double client_seq = 0;
      mg_json_get_num(hm->body, "$.seq", &client_seq);
      uint32_t seq = (uint32_t)client_seq;

      char *buf = malloc(16384);
      if (!buf) {
        mg_http_reply(c, 500, "", "{\"error\":\"out of memory\"}");
        return;
      }

      size_t remaining = 16384;
      int off =
          snprintf(buf, remaining, "{\"next_seq\":%u,\"lines\":[", g_log_seq);
      if (off < 0 || (size_t)off >= remaining) {
        free(buf);
        mg_http_reply(c, 500, "", "{\"error\":\"buffer overflow\"}");
        return;
      }
      remaining -= off;
      bool first = true;

      for (int i = 0; i < MAX_LOG_LINES; i++) {
        int idx = (g_log_idx + i) % MAX_LOG_LINES;
        if (g_log_seqs[idx] > seq) {
          int written = snprintf(buf + off, remaining, "%s\"%s\"",
                                 first ? "" : ",", g_log_buf[idx]);
          if (written < 0 || (size_t)written >= remaining) {
            break; // Truncate at buffer limit
          }
          off += written;
          remaining -= written;
          first = false;
        }
      }
      if (remaining > 3) {
        strncat(buf, "]}", remaining - 1);
      }
      mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", buf);
      free(buf);
    }
    // 6. API: Events
    else if (mg_strcmp(hm->uri, mg_str("/events")) == 0) {
      event_log_entry_t events[100];
      uint8_t count = 0;
      system_monitoring_get_all_events(events, &count);
      char *buf = malloc(8192);
      if (buf) {
        size_t remaining = 8192;
        int off = snprintf(buf, remaining, "[");
        if (off < 0 || (size_t)off >= remaining) {
          free(buf);
          mg_http_reply(c, 500, "", "{\"error\":\"buffer overflow\"}");
          return;
        }
        remaining -= off;

        for (int i = 0; i < count; i++) {
          int written =
              snprintf(buf + off, remaining,
                       "%s{\"timestamp\":%u,\"event_type\":%d,\"zone_id\":%d}",
                       i > 0 ? "," : "", (unsigned int)events[i].timestamp,
                       events[i].event_type, events[i].zone_id);

          if (written < 0 || (size_t)written >= remaining) {
            break; // Truncate at buffer limit
          }
          off += written;
          remaining -= written;
        }

        if (remaining > 2) {
          strncat(buf, "]", remaining - 1);
        }
        mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", buf);
        free(buf);
      }
    }

    // 6a. API: Login (Create Session Token)
    else if (mg_strcmp(hm->uri, mg_str("/api/login")) == 0) {
      char pin_in[STR_SMALL] = {0};
      get_json_str(hm->body, "$.pin", pin_in, sizeof(pin_in));

      user_t *user = NULL;
      if (user_authenticate_pin(pin_in, &user) && user != NULL) {
        // Get client IP address
        char client_ip[16] = {0};
        struct mg_str *forwarded = mg_http_get_header(hm, "X-Forwarded-For");
        if (forwarded && forwarded->len > 0 &&
            forwarded->len < sizeof(client_ip)) {
          snprintf(client_ip, sizeof(client_ip), "%.*s", (int)forwarded->len,
                   forwarded->buf);
        } else {
          // Fall back to connection remote address
          mg_snprintf(client_ip, sizeof(client_ip), "%M", mg_print_ip, &c->rem);
        }

        // Create session token
        char token[SESSION_TOKEN_MAX_LEN] = {0};
        if (session_create(user->name, client_ip, token)) {
          printf("[LOGIN] Session created for user '%s' from IP %s\n",
                 user->name, client_ip);
          mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"status\":\"ok\",\"token\":\"%s\",\"user\":\"%s\","
                        "\"is_admin\":%s}",
                        token, user->name, user->is_admin ? "true" : "false");
        } else {
          printf("[LOGIN] Failed to create session for user '%s'\n",
                 user->name);
          mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                        "{\"status\":\"error\",\"message\":\"Failed to create "
                        "session\"}");
        }
      } else {
        printf("[LOGIN] Login failed for PIN\n");
        mg_http_reply(c, 401, "Content-Type: application/json\r\n",
                      "{\"status\":\"error\",\"message\":\"Invalid PIN\"}");
      }
    }

    // 6b. API: Arm/Disarm (POST)
    // --- FIXED ARM HANDLER ---
    else if (mg_strcmp(hm->uri, mg_str("/api/arm")) == 0) {
      char pin_in[16] = {0};
      char mode_str[16] = {0};
      get_json_str(hm->body, "$.pin", pin_in, sizeof(pin_in));
      get_json_str(hm->body, "$.mode", mode_str, sizeof(mode_str));

      keypad_result_t res = engine_check_keypad(pin_in);
      if (res.authenticated) {
        // Parse arm mode from request
        arm_mode_t mode = ARM_AWAY; // Default
        if (strcmp(mode_str, "stay") == 0)
          mode = ARM_STAY;
        else if (strcmp(mode_str, "night") == 0)
          mode = ARM_NIGHT;
        else if (strcmp(mode_str, "vacation") == 0)
          mode = ARM_VACATION;

        engine_ui_arm(mode);
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"status\":\"ok\",\"user\":\"%s\",\"mode\":\"%s\"}",
                      res.name, mode_str);
      } else {
        mg_http_reply(c, 401, "Content-Type: application/json\r\n",
                      "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
      }
    }
    // --- FIXED DISARM HANDLER ---
    else if (mg_strcmp(hm->uri, mg_str("/api/disarm")) == 0) {
      char pin_in[16] = {0};
      get_json_str(hm->body, "$.pin", pin_in, sizeof(pin_in));

      keypad_result_t res = engine_check_keypad(pin_in);
      if (res.authenticated) {
        engine_ui_disarm();
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"status\":\"ok\",\"user\":\"%s\"}", res.name);
      } else {
        mg_http_reply(c, 401, "Content-Type: application/json\r\n",
                      "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
      }
    }
    // 7. API: Zones (for zstatus.html)
    else if (mg_strcmp(hm->uri, mg_str("/api/zones")) == 0) {
      char *z_buf = malloc(16384);
      int off = snprintf(z_buf, 16384, "[");

      // Add physical zones
      for (int i = 0; i < storage_get_zone_count(); i++) {
        off += snprintf(
            z_buf + off, 16384 - off,
            "{\"id\":%d,\"name\":\"%s\",\"location\":\"%s\",\"open\":%s},",
            storage_get_zone(i)->id, storage_get_zone(i)->description,
            storage_get_zone(i)->location,
            (digitalRead(storage_get_zone(i)->gpio) == 0) ? "true" : "false");
      }

      // Add ESPHome virtual zones
      for (int i = 0; i < storage_get_esphome_count(); i++) {
        if (storage_get_esphome_device(i)->enabled) {
          off +=
              snprintf(z_buf + off, 16384 - off,
                       "{\"id\":%d,\"name\":\"%s "
                       "(ESPHome)\",\"location\":\"Virtual\",\"open\":false},",
                       storage_get_esphome_device(i)->virtual_zone_start,
                       storage_get_esphome_device(i)->friendly_name);
        }
      }

      // Add Tuya virtual zones
      tuya_device_t tuya_devs[TUYA_DEVICE_MAX_COUNT];
      int tuya_count = 0;
      if (tuya_mgr_get_devices(tuya_devs, TUYA_DEVICE_MAX_COUNT, &tuya_count) ==
          ESP_OK) {
        for (int i = 0; i < tuya_count; i++) {
          if (tuya_devs[i].virtual_zone_id > 0 && tuya_devs[i].enabled) {
            off += snprintf(z_buf + off, 16384 - off,
                            "{\"id\":%d,\"name\":\"%s "
                            "(Tuya)\",\"location\":\"Virtual\",\"open\":%s},",
                            tuya_devs[i].virtual_zone_id, tuya_devs[i].name,
                            tuya_devs[i].contact_open ? "true" : "false");
          }
        }
      }

      // Remove trailing comma if present
      if (off > 1 && z_buf[off - 1] == ',') {
        z_buf[off - 1] = '\0';
        off--;
      }

      strcat(z_buf, "]");
      mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", z_buf);
      free(z_buf);
    }
    // 8. API: Zone Trigger
    else if (mg_strcmp(hm->uri, mg_str("/api/trigger")) == 0) {
      char id_buf[10] = {0};
      mg_http_get_var(&hm->query, "id", id_buf, sizeof(id_buf));
      int zone_id = atoi(id_buf);

      // Physical zones (1-32) - use GPIO pin
      if (zone_id >= 1 && zone_id <= 32) {
        for (int i = 0; i < storage_get_zone_count(); i++) {
          if (storage_get_zone(i)->id == zone_id) {
            mock_gpio_pins[storage_get_zone(i)->gpio] =
                (mock_gpio_pins[storage_get_zone(i)->gpio] == 1) ? 0 : 1;
            break;
          }
        }
      }
      // ESPHome zones (33-64) - toggle state in device
      else if (zone_id >= 33 && zone_id <= 64) {
        // In a real implementation, this would trigger ESPHome device state
        // change
        printf("[ESPHome] Zone %d triggered (mock)\n", zone_id);
      }
      // Tuya zones (67-96) - toggle state in device
      else if (zone_id >= 67 && zone_id <= 96) {
        tuya_device_t tuya_devs[TUYA_DEVICE_MAX_COUNT];
        int tuya_count = 0;
        if (tuya_mgr_get_devices(tuya_devs, TUYA_DEVICE_MAX_COUNT,
                                 &tuya_count) == ESP_OK) {
          for (int i = 0; i < tuya_count; i++) {
            if (tuya_devs[i].virtual_zone_id == zone_id) {
              // Toggle the sensor state for testing
              printf("[Tuya] Zone %d (%s) triggered (mock)\n", zone_id,
                     tuya_devs[i].name);
              break;
            }
          }
        }
      }

      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\"}");
    }
    // 11. API: Relay Toggle (POST)
    else if (mg_strcmp(hm->uri, mg_str("/api/relay/toggle")) == 0) {
      double idx_d = 0;
      bool state_b = false;
      mg_json_get_num(hm->body, "$.index", &idx_d);
      mg_json_get_bool(hm->body, "$.state", &state_b);

      int relay_idx = (int)idx_d;
      printf("[DEBUG] Relay toggle request: index=%d, state=%s\n", relay_idx,
             state_b ? "ON" : "OFF");
      printf("[DEBUG] Available: %d physical relays, %d ESPHome devices\n",
             storage_get_relay_count(), storage_get_esphome_count());

      // Find the relay by array index
      bool found = false;

      // Check if index is valid
      if (relay_idx >= 0 && relay_idx < storage_get_relay_count()) {
        if (storage_get_relay(relay_idx)->gpio == -1) {
          // Virtual relay - ESPHome
          printf("[DEBUG] Virtual relay at index %d, id=%d\n", relay_idx,
                 storage_get_relay(relay_idx)->id);

          // Find the ESPHome entity mapped to this relay
          char hostname[64];
          uint32_t entity_key;
          extern int esphome_api_find_entity_by_relay(
              int relay_id, char *hostname_out, size_t hostname_len,
              uint32_t *entity_key_out);

          if (esphome_api_find_entity_by_relay(storage_get_relay(relay_idx)->id,
                                               hostname, sizeof(hostname),
                                               &entity_key) == 0) {
            printf("[ESPHome] Relay %d found on device %s, entity_key=%" PRIu32
                   "\n",
                   storage_get_relay(relay_idx)->id, hostname, entity_key);

            // Call ESPHome API
            printf(
                "[DEBUG] About to call esphome_api_set_switch for relay %d\n",
                storage_get_relay(relay_idx)->id);
            extern int esphome_api_set_switch(const char *hostname,
                                              uint32_t entity_key, bool state);
            printf("[DEBUG] Calling esphome_api_set_switch with hostname=%s, "
                   "entity_key=%" PRIu32 ", state=%s\n",
                   hostname, entity_key, state_b ? "ON" : "OFF");
            int result = esphome_api_set_switch(hostname, entity_key, state_b);
            printf("[DEBUG] esphome_api_set_switch returned: %d\n", result);

            if (result == 0) {
              // Update mock state for ESPHome relay
              int esphome_relay_idx = storage_get_relay(relay_idx)->id - 8;
              if (esphome_relay_idx >= 0 && esphome_relay_idx < 24) {
                mock_esphome_relay_states[esphome_relay_idx] = state_b;
                printf("[DEBUG] Set ESPHome relay %d (idx %d) to %s\n",
                       storage_get_relay(relay_idx)->id, esphome_relay_idx,
                       state_b ? "ON" : "OFF");
              }
              found = true;
            } else {
              printf("[ERROR] ESPHome API call failed for relay %d\n",
                     storage_get_relay(relay_idx)->id);
            }
          } else {
            printf("[DEBUG] No ESPHome entity mapped to relay %d\n",
                   storage_get_relay(relay_idx)->id);
          }
        } else {
          // Physical relay
          mock_relay_states[relay_idx] = state_b;
          printf("[Relay] Physical relay %d (%s) set to %s\n",
                 storage_get_relay(relay_idx)->id,
                 storage_get_relay(relay_idx)->name, state_b ? "ON" : "OFF");
          found = true;
        }
      }

      // Check Tuya relays
      if (!found) {
        tuya_device_t tuya_devs[TUYA_DEVICE_MAX_COUNT];
        int tuya_count = 0;
        if (tuya_mgr_get_devices(tuya_devs, TUYA_DEVICE_MAX_COUNT,
                                 &tuya_count) == ESP_OK) {
          for (int i = 0; i < tuya_count && !found; i++) {
            if (tuya_devs[i].virtual_relay_id == relay_idx &&
                tuya_devs[i].enabled) {
              tuya_mgr_set_switch(tuya_devs[i].device_id, state_b);
              printf("[Tuya] Relay %d (%s) set to %s\n",
                     tuya_devs[i].virtual_relay_id, tuya_devs[i].name,
                     state_b ? "ON" : "OFF");
              found = true;
              break;
            }
          }
        }
      }

      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\"}");
    }
    // 12. API: RF Inject (POST)
    else if (mg_strcmp(hm->uri, mg_str("/api/rf/inject")) == 0) {
      get_json_str(hm->body, "$.code", mock_last_rf, sizeof(mock_last_rf));
      printf("\n[RF] Signal Injected: %s\n", mock_last_rf);
      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\"}");
    }
    // 9. Serve Static Files
    // 9. API: Network Settings (POST)
    else if (mg_strcmp(hm->uri, mg_str("/api/network")) == 0) {
      bool use_dhcp = false;
      if (mg_json_get_bool(hm->body, "$.use_dhcp", &use_dhcp)) {
        storage_get_network()->use_dhcp = use_dhcp;
      }
      get_json_str(hm->body, "$.ip", storage_get_network()->ip,
                   sizeof(storage_get_network()->ip));
      get_json_str(hm->body, "$.netmask", storage_get_network()->netmask,
                   sizeof(storage_get_network()->netmask));
      get_json_str(hm->body, "$.gateway", storage_get_network()->gateway,
                   sizeof(storage_get_network()->gateway));
      get_json_str(hm->body, "$.dns", storage_get_network()->dns,
                   sizeof(storage_get_network()->dns));

      storage_save_network();
      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\"}");
    }
    // 11. API: Test Password Hashing (POST)
    else if (mg_strcmp(hm->uri, mg_str("/api/test/hash")) == 0) {
      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"success\":true,\"message\":\"Hashing is disabled\"}");
    }
    // 12. API: Test Session Management (POST)
    else if (mg_strcmp(hm->uri, mg_str("/api/test/session")) == 0) {
      char username[32] = "";
      get_json_str(hm->body, "$.username", username, sizeof(username));

      char token[SESSION_TOKEN_MAX_LEN];
      if (session_create(username, "127.0.0.1", token)) {
        // Test validation
        char validated_user[SESSION_USERNAME_MAX_LEN];
        bool valid = session_validate(token, "127.0.0.1", validated_user);

        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"success\":true,\"token\":\"%s\",\"validated\":%s,"
                      "\"user\":\"%s\"}",
                      token, valid ? "true" : "false", validated_user);
      } else {
        mg_http_reply(
            c, 500, "Content-Type: application/json\r\n",
            "{\"success\":false,\"error\":\"Session creation failed\"}");
      }
    }
    // 13. API: Get Audit Log (GET)
    else if (mg_strcmp(hm->uri, mg_str("/api/audit")) == 0) {
      char json[8192];
      if (audit_log_to_json(json, sizeof(json), 50)) {
        mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json);
      } else {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"Failed to generate audit log\"}");
      }
    }
    // 14. API: Matter Devices List (GET)
    else if (mg_strcmp(hm->uri, mg_str("/api/matter/devices")) == 0) {
      extern void mock_matter_list_devices(void);
      mock_matter_list_devices();
      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"see console for device list\"}");
    }
    // 15. API: Matter Trigger Sensor (POST)
    else if (mg_strcmp(hm->uri, mg_str("/api/matter/trigger")) == 0) {
      char zone_str[16] = {0};
      get_json_str(hm->body, "$.zone_id", zone_str, sizeof(zone_str));
      int zone_id = atoi(zone_str);

      extern void mock_matter_trigger_sensor(int zone_id);
      mock_matter_trigger_sensor(zone_id);

      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"triggered\",\"zone\":%d}", zone_id);
    }
    // 16. API: ESPHome Devices List (GET)
    else if (mg_strcmp(hm->uri, mg_str("/api/esphome/devices")) == 0) {
      extern char *mock_esphome_get_json(void);
      char *json = mock_esphome_get_json();
      mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json);
    }
    // 17. API: ESPHome Trigger Sensor (POST)
    else if (mg_strcmp(hm->uri, mg_str("/api/esphome/trigger")) == 0) {
      char entity_id[64] = {0};
      char state_str[16] = {0};
      get_json_str(hm->body, "$.entity_id", entity_id, sizeof(entity_id));
      get_json_str(hm->body, "$.state", state_str, sizeof(state_str));
      bool state = (strcmp(state_str, "true") == 0);

      extern void mock_esphome_trigger_sensor(const char *entity_id,
                                              bool state);
      mock_esphome_trigger_sensor(entity_id, state);

      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"triggered\",\"entity\":\"%s\",\"state\":%s}",
                    entity_id, state ? "true" : "false");
    }
    // 18. API: ESPHome Control Switch (POST)
    else if (mg_strcmp(hm->uri, mg_str("/api/esphome/control")) == 0) {
      char entity_id[64] = {0};
      char state_str[16] = {0};
      get_json_str(hm->body, "$.entity_id", entity_id, sizeof(entity_id));
      get_json_str(hm->body, "$.state", state_str, sizeof(state_str));
      bool state = (strcmp(state_str, "true") == 0);

      // Try real ESPHome API first
      int real_result = -1;
      for (int i = 0; i < storage_get_esphome_count(); i++) {
        esphome_device_t *dev = storage_get_esphome_device(i);
        char hostname_short[STR_SMALL];
        strncpy(hostname_short, dev->hostname, sizeof(hostname_short) - 1);
        char *dot = strstr(hostname_short, ".local");
        if (dot)
          *dot = '\0';

        if (strcmp(hostname_short, entity_id) == 0 ||
            strcmp(dev->friendly_name, entity_id) == 0) {
          if (dev->enabled && dev->entity_key != 0) {
            printf("[REAL] Trying to control %s (%s) -> state %s\n",
                   dev->friendly_name, dev->hostname, state ? "ON" : "OFF");
            real_result =
                esphome_api_set_switch(dev->hostname, dev->entity_key, state);
            if (real_result == 0) {
              printf("[REAL] Successfully controlled %s\n", dev->friendly_name);
            } else {
              printf("[REAL] Failed to control %s (error %d)\n",
                     dev->friendly_name, real_result);
            }
          }
          break;
        }
      }

      // Fall back to mock if real API failed or no real device found
      if (real_result != 0) {
        extern void mock_esphome_set_switch(const char *entity_id, bool state);
        printf("[MOCK] Using mock control for %s\n", entity_id);
        mock_esphome_set_switch(entity_id, state);
      }

      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"success\",\"entity\":\"%s\",\"state\":%s,"
                    "\"method\":\"%s\"}",
                    entity_id, state ? "true" : "false",
                    (real_result == 0) ? "real" : "mock");
    }
    // 19. API: ESPHome Device Management - List (GET)
    else if (mg_strcmp(hm->uri, mg_str("/api/esphome")) == 0) {
      // Read esphome.json file and return it
      FILE *f = fopen("data/esphome.json", "r");
      if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *json = malloc(size + 1);
        if (json) {
          fread(json, 1, size, f);
          json[size] = '\0';
          mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s",
                        json);
          free(json);
        } else {
          mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                        "{\"error\":\"Memory allocation failed\"}");
        }
        fclose(f);
      } else {
        mg_http_reply(c, 200, "Content-Type: application/json\r\n", "[]");
      }
    }
    // 20. API: ESPHome Device Management - Add (POST)
    else if (mg_strcmp(hm->uri, mg_str("/api/esphome/add")) == 0) {
      char hostname[STR_SMALL] = {0}, friendly_name[STR_SMALL] = {0};
      char password[STR_SMALL] = {0}, encryption_key[STR_MEDIUM] = {0};
      char port_buf[10] = {0}, vzs_buf[10] = {0}, vrs_buf[10] = {0},
           enabled_buf[10] = {0};

      get_json_str(hm->body, "$.hostname", hostname, sizeof(hostname));
      get_json_str(hm->body, "$.port", port_buf, sizeof(port_buf));
      get_json_str(hm->body, "$.friendly_name", friendly_name,
                   sizeof(friendly_name));
      get_json_str(hm->body, "$.password", password, sizeof(password));
      get_json_str(hm->body, "$.encryption_key", encryption_key,
                   sizeof(encryption_key));
      get_json_str(hm->body, "$.virtual_zone_start", vzs_buf, sizeof(vzs_buf));
      get_json_str(hm->body, "$.virtual_relay_start", vrs_buf, sizeof(vrs_buf));
      get_json_str(hm->body, "$.enabled", enabled_buf, sizeof(enabled_buf));

      uint16_t port = strlen(port_buf) ? atoi(port_buf) : 6053;
      int8_t vzs = strlen(vzs_buf) ? atoi(vzs_buf) : 33;
      int8_t vrs = strlen(vrs_buf) ? atoi(vrs_buf) : 8;
      bool enabled =
          strlen(enabled_buf) ? (strcmp(enabled_buf, "true") == 0) : true;

      int result = esphome_device_add(
          hostname, port, friendly_name, strlen(password) ? password : NULL,
          strlen(encryption_key) ? encryption_key : NULL, vzs, vrs, enabled);

      if (result == 0) {
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"status\":\"ok\"}");
      } else {
        mg_http_reply(
            c, 500, "Content-Type: application/json\r\n",
            "{\"status\":\"error\",\"message\":\"Failed to add device\"}");
      }
    }
    // 21. API: ESPHome Device Management - Update (POST)
    else if (mg_strcmp(hm->uri, mg_str("/api/esphome/update")) == 0) {
      char hostname[STR_SMALL] = {0}, friendly_name[STR_SMALL] = {0};
      char password[STR_SMALL] = {0}, encryption_key[STR_MEDIUM] = {0};
      char port_buf[10] = {0}, vzs_buf[10] = {0}, vrs_buf[10] = {0},
           enabled_buf[10] = {0};

      get_json_str(hm->body, "$.hostname", hostname, sizeof(hostname));
      get_json_str(hm->body, "$.port", port_buf, sizeof(port_buf));
      get_json_str(hm->body, "$.friendly_name", friendly_name,
                   sizeof(friendly_name));
      get_json_str(hm->body, "$.password", password, sizeof(password));
      get_json_str(hm->body, "$.encryption_key", encryption_key,
                   sizeof(encryption_key));
      get_json_str(hm->body, "$.virtual_zone_start", vzs_buf, sizeof(vzs_buf));
      get_json_str(hm->body, "$.virtual_relay_start", vrs_buf, sizeof(vrs_buf));
      get_json_str(hm->body, "$.enabled", enabled_buf, sizeof(enabled_buf));

      uint16_t port = strlen(port_buf) ? atoi(port_buf) : 0; // 0 = no change
      int8_t vzs = strlen(vzs_buf) ? atoi(vzs_buf) : -1;     // -1 = no change
      int8_t vrs = strlen(vrs_buf) ? atoi(vrs_buf) : -1;     // -1 = no change
      bool enabled =
          strlen(enabled_buf) ? (strcmp(enabled_buf, "true") == 0) : true;

      int result = esphome_device_update(
          hostname, port, strlen(friendly_name) ? friendly_name : NULL,
          strlen(password) ? password : NULL,
          strlen(encryption_key) ? encryption_key : NULL, vzs, vrs, enabled);

      if (result == 0) {
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"status\":\"ok\"}");
      } else {
        mg_http_reply(
            c, 404, "Content-Type: application/json\r\n",
            "{\"status\":\"error\",\"message\":\"Device not found\"}");
      }
    }
    // 22. API: ESPHome Device Management - Delete (POST)
    else if (mg_strcmp(hm->uri, mg_str("/api/esphome/delete")) == 0) {
      char hostname[STR_SMALL] = {0};
      get_json_str(hm->body, "$.hostname", hostname, sizeof(hostname));

      int result = esphome_device_delete(hostname);
      if (result == 0) {
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"status\":\"ok\"}");
      } else {
        mg_http_reply(
            c, 404, "Content-Type: application/json\r\n",
            "{\"status\":\"error\",\"message\":\"Device not found\"}");
      }
    }

    // ESPHome Testing/Connection Endpoints (for tester.html)
    else if (mg_strcmp(hm->uri, mg_str("/api/esphome/connect")) == 0) {
      char host[STR_SMALL] = {0};
      char port_buf[10] = {0};
      char password[STR_SMALL] = {0};

      get_json_str(hm->body, "$.host", host, sizeof(host));
      get_json_str(hm->body, "$.port", port_buf, sizeof(port_buf));
      get_json_str(hm->body, "$.password", password, sizeof(password));

      if (strlen(host) == 0) {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"status\":\"error\",\"message\":\"Missing host\"}");
        return;
      }

      // Return mock success for devices in config
      bool found = false;
      char device_name[STR_SMALL] = {0};
      for (int i = 0; i < storage_get_esphome_count(); i++) {
        if (strcmp(storage_get_esphome_device(i)->hostname, host) == 0) {
          found = true;
          strncpy(device_name, storage_get_esphome_device(i)->friendly_name,
                  STR_SMALL - 1);
          break;
        }
      }

      if (found) {
        mg_http_reply(
            c, 200, "Content-Type: application/json\r\n",
            "{\"status\":\"ok\",\"device_name\":\"%s\",\"entities\":0}",
            device_name);
      } else {
        mg_http_reply(
            c, 200, "Content-Type: application/json\r\n",
            "{\"status\":\"ok\",\"device_name\":\"%s\",\"entities\":0}", host);
      }
    } else if (mg_strcmp(hm->uri, mg_str("/api/esphome/status")) == 0) {
      char host[STR_SMALL] = {0};
      get_json_str(hm->body, "$.host", host, sizeof(host));

      if (strlen(host) == 0) {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"status\":\"error\",\"message\":\"Missing host\"}");
        return;
      }

      // Check enabled status from config
      bool connected = false;
      for (int i = 0; i < storage_get_esphome_count(); i++) {
        if (strcmp(storage_get_esphome_device(i)->hostname, host) == 0) {
          connected = storage_get_esphome_device(i)->enabled;
          break;
        }
      }

      mg_http_reply(
          c, 200, "Content-Type: application/json\r\n",
          "{\"connected\":%s,\"uptime\":\"0s\",\"version\":\"unknown\"}",
          connected ? "true" : "false");
    } else if (mg_strcmp(hm->uri, mg_str("/api/esphome/entities")) == 0) {
      char host[STR_SMALL] = {0};
      get_json_str(hm->body, "$.host", host, sizeof(host));

      if (strlen(host) == 0) {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"status\":\"error\",\"message\":\"Missing host\"}");
        return;
      }

      // Return empty list
      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"entities\":[]}");
    } else if (mg_strcmp(hm->uri, mg_str("/api/esphome/subscribe")) == 0) {
      char host[STR_SMALL] = {0};
      get_json_str(hm->body, "$.host", host, sizeof(host));

      if (strlen(host) == 0) {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"status\":\"error\",\"message\":\"Missing host\"}");
        return;
      }

      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\",\"subscribed\":true}");
    }

    // ==================== CAMERA API ====================

    // GET /api/cameras - List all cameras
    else if (mg_strcmp(hm->uri, mg_str("/api/cameras")) == 0) {
      // Build JSON response with proper structure
      char response[4096];
      int offset = snprintf(response, sizeof(response), "{\"cameras\":[");
      int cam_count = camera_mgr_get_count();
      for (int i = 0; i < cam_count; i++) {
        const camera_device_t *cam = camera_mgr_get_device(i);
        if (cam) {
          offset += snprintf(
              response + offset, sizeof(response) - offset,
              "%s{\"id\":%d,\"hostname\":\"%s\",\"port\":%d,\"friendly_name\":"
              "\"%s\",\"esphome_id\":\"%s\",\"enabled\":%s,\"sd_recording_"
              "enabled\":%s,\"last_snapshot\":%ld,\"failures\":%d}",
              i > 0 ? "," : "", i, cam->hostname, cam->port, cam->friendly_name,
              cam->camera_id, cam->enabled ? "true" : "false",
              cam->sd_recording_enabled ? "true" : "false",
              (long)cam->last_snapshot_time, cam->consecutive_failures);
        }
      }
      snprintf(response + offset, sizeof(response) - offset,
               "],\"sd_available\":%s,\"stats\":{\"total_snapshots\":0,\"alarm_"
               "triggered_snapshots\":0}}",
               camera_mgr_is_sd_available() ? "true" : "false");
      mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s",
                    response);
    }

    // GET /api/camera/:id/snapshot - Get camera snapshot
    else if (mg_match(hm->uri, mg_str("/api/camera/*/snapshot"), NULL)) {
      const char *id_start = strstr(hm->uri.buf, "/api/camera/");
      if (id_start) {
        id_start += 12;
        int camera_id = atoi(id_start);

        uint8_t *buffer = NULL;
        size_t size = 0;

        if (camera_mgr_fetch_snapshot(camera_id, &buffer, &size) == 0 &&
            buffer && size > 0) {
          printf("[SNAPSHOT] Sending JPEG with Content-Length: %zu\n", size);
          mg_printf(
              c,
              "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nCache-Control: "
              "no-cache\r\nContent-Length: %zu\r\n\r\n",
              size);
          fflush(stdout);
          mg_send(c, buffer, size);
          fflush(stdout);
        } else {
          mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                        "{\"error\":\"Failed to fetch snapshot\"}");
        }
      } else {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"Invalid camera ID\"}");
      }
    }

    // GET /api/camera/:id/stream - MJPEG streaming proxy (mock)
    else if (mg_match(hm->uri, mg_str("/api/camera/*/stream"), NULL)) {
      const char *id_start = strstr(hm->uri.buf, "/api/camera/");
      if (id_start) {
        id_start += 12;
        int camera_id = atoi(id_start);
        const camera_device_t *cam = camera_mgr_get_device(camera_id);
        if (!cam || !cam->enabled) {
          mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                        "{\"error\":\"Camera not found or disabled\"}");
          return;
        }
        printf("[MOCK] MJPEG stream proxy for camera %d (%s)\n", camera_id,
               cam->friendly_name);
        if (strlen(cam->stream_url) == 0) {
          mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                        "{\"error\":\"No stream_url configured\"}");
          return;
        }
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "curl -s --max-time 30 --connect-timeout 5 '%s'",
                 cam->stream_url);
        FILE *fp = popen(cmd, "r");
        if (!fp) {
          mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                        "{\"error\":\"Failed to start curl\"}");
          return;
        }
        mg_printf(c,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                  "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                  "Pragma: no-cache\r\n"
                  "Connection: close\r\n"
                  "\r\n");
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
          mg_send(c, buf, n);
        }
        pclose(fp);
        c->is_draining = 1;
      } else {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"Invalid camera ID\"}");
      }
    }

    // POST /api/camera/add - Add new camera
    else if (mg_strcmp(hm->uri, mg_str("/api/camera/add")) == 0) {
      char hostname[64] = {0};
      char name[64] = {0};
      char camera_id[32] = {0};
      char username[32] = {0};
      char password[64] = {0};
      char snapshot_cmd[256] = {0};
      char motion_cmd[256] = {0};
      int port = 80;
      bool enabled = true;

      get_json_str(hm->body, "$.hostname", hostname, sizeof(hostname));
      get_json_str(hm->body, "$.friendly_name", name, sizeof(name));
      get_json_str(hm->body, "$.camera_id", camera_id, sizeof(camera_id));
      get_json_str(hm->body, "$.username", username, sizeof(username));
      get_json_str(hm->body, "$.password", password, sizeof(password));
      get_json_str(hm->body, "$.snapshot_cmd", snapshot_cmd,
                   sizeof(snapshot_cmd));
      get_json_str(hm->body, "$.motion_cmd", motion_cmd, sizeof(motion_cmd));
      double port_d = 0;
      mg_json_get_num(hm->body, "$.port", &port_d);
      if (port_d > 0)
        port = (int)port_d;
      mg_json_get_bool(hm->body, "$.enabled", &enabled);

      if (strlen(hostname) > 0 && strlen(name) > 0) {
        int camera_id_result = camera_mgr_add_device(
            hostname, port, name,
            strlen(camera_id) > 0 ? camera_id : "image.jpg",
            strlen(username) > 0 ? username : "",
            strlen(password) > 0 ? password : "",
            strlen(snapshot_cmd) > 0 ? snapshot_cmd : "",
            strlen(motion_cmd) > 0 ? motion_cmd : "", enabled);
        if (camera_id_result >= 0) {
          mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"success\":true,\"camera_id\":%d}",
                        camera_id_result);
        } else {
          mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                        "{\"error\":\"Failed to add camera\"}");
        }
      } else {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"Missing required fields\"}");
      }
    }

    // DELETE /api/camera/:id - Remove camera
    else if (mg_match(hm->uri, mg_str("/api/camera/*"), NULL) &&
             mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
      const char *id_start = strstr(hm->uri.buf, "/api/camera/");
      if (id_start) {
        id_start += 12;
        int camera_id = atoi(id_start);

        if (camera_mgr_remove_device(camera_id) == 0) {
          mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"success\":true}");
        } else {
          mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                        "{\"error\":\"Failed to delete camera\"}");
        }
      } else {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"Invalid camera ID\"}");
      }
    }

    // POST /api/camera/:id/toggle - Enable/disable camera
    else if (mg_match(hm->uri, mg_str("/api/camera/*/toggle"), NULL)) {
      const char *id_start = strstr(hm->uri.buf, "/api/camera/");
      if (id_start) {
        id_start += 12;
        int camera_id = atoi(id_start);

        const camera_device_t *cam = camera_mgr_get_device(camera_id);
        if (cam) {
          if (camera_mgr_set_enabled(camera_id, !cam->enabled) == 0) {
            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                          "{\"success\":true,\"enabled\":%s}",
                          !cam->enabled ? "true" : "false");
          } else {
            mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                          "{\"error\":\"Failed to toggle camera\"}");
          }
        } else {
          mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                        "{\"error\":\"Camera not found\"}");
        }
      } else {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"Invalid camera ID\"}");
      }
    }

    // POST /api/camera/:id/test - Test camera connection
    else if (mg_match(hm->uri, mg_str("/api/camera/*/test"), NULL)) {
      const char *id_start = strstr(hm->uri.buf, "/api/camera/");
      if (id_start) {
        id_start += 12;
        int camera_id = atoi(id_start);

        if (camera_mgr_test_connection(camera_id) == 0) {
          mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"success\":true,\"status\":\"online\"}");
        } else {
          mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"success\":false,\"status\":\"offline\"}");
        }
      } else {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"Invalid camera ID\"}");
      }
    }

    // ==================== LIVE CAMERA VIEW ====================

    // GET /cameras_live.html - Live camera grid view
    else if (mg_strcmp(hm->uri, mg_str("/cameras_live.html")) == 0) {
      struct mg_http_serve_opts opts = {.root_dir = "data"};
      mg_http_serve_file(c, hm, "data/cameras_live.html", &opts);
    }

    // ==================== TUYA DEVICE API (MOCK) ====================

    // GET /api/tuya/devices - List all Tuya devices
    else if (mg_strcmp(hm->uri, mg_str("/api/tuya/devices")) == 0) {
      tuya_device_t devices[TUYA_DEVICE_MAX_COUNT];
      int count = 0;

      tuya_mgr_get_devices(devices, TUYA_DEVICE_MAX_COUNT, &count);

      mg_printf(c, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n");
      mg_printf(c, "{\"connected\":%s,\"count\":%d,\"devices\":[",
                tuya_mgr_is_connected() ? "true" : "false", count);

      for (int i = 0; i < count; i++) {
        if (i > 0)
          mg_printf(c, ",");
        mg_printf(c,
                  "{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"%s\","
                  "\"state\":\"%s\",\"enabled\":%s",
                  devices[i].device_id, devices[i].name,
                  tuya_device_type_name(devices[i].type),
                  devices[i].state == TUYA_DEV_STATE_ONLINE ? "online"
                                                            : "offline",
                  devices[i].enabled ? "true" : "false");

        if (devices[i].virtual_zone_id >= 0) {
          mg_printf(c, ",\"zone_id\":%d", devices[i].virtual_zone_id);
        }
        if (devices[i].virtual_relay_id >= 0) {
          mg_printf(c, ",\"relay_id\":%d", devices[i].virtual_relay_id);
        }

        if (devices[i].capabilities & TUYA_CAP_SWITCH) {
          mg_printf(c, ",\"switch_on\":%s",
                    devices[i].switch_on ? "true" : "false");
        }
        if (devices[i].capabilities & TUYA_CAP_BRIGHTNESS) {
          mg_printf(c, ",\"brightness\":%d", devices[i].brightness);
        }
        if (devices[i].capabilities & TUYA_CAP_CONTACT) {
          mg_printf(c, ",\"contact_open\":%s",
                    devices[i].contact_open ? "true" : "false");
        }
        if (devices[i].capabilities & TUYA_CAP_MOTION) {
          mg_printf(c, ",\"motion\":%s",
                    devices[i].motion_detected ? "true" : "false");
        }
        if (devices[i].capabilities & TUYA_CAP_BATTERY) {
          mg_printf(c, ",\"battery\":%d", devices[i].battery);
        }

        mg_printf(c, "}");
      }
      mg_printf(c, "]}");
      c->is_resp = 0;
    }

    // POST /api/tuya/discover
    else if (mg_strcmp(hm->uri, mg_str("/api/tuya/discover")) == 0) {
      tuya_mgr_discover_devices(10000);
      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\",\"message\":\"Discovery started\"}");
    }

    // POST /api/tuya/pair
    else if (mg_strcmp(hm->uri, mg_str("/api/tuya/pair")) == 0) {
      tuya_mgr_enable_pairing(60);
      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"status\":\"ok\",\"duration\":60}");
    }

    // DELETE /api/tuya/device/:id
    else if (mg_match(hm->uri, mg_str("/api/tuya/device/*"), NULL) &&
             mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
      const char *id_start = strstr(hm->uri.buf, "/api/tuya/device/");
      if (id_start) {
        id_start += 17;
        char device_id[TUYA_DEVICE_ID_MAX_LEN];
        int id_len = 0;
        while (id_start[id_len] && id_start[id_len] != '/' &&
               id_len < TUYA_DEVICE_ID_MAX_LEN - 1) {
          device_id[id_len] = id_start[id_len];
          id_len++;
        }
        device_id[id_len] = '\0';

        if (tuya_mgr_remove_device(device_id) == ESP_OK) {
          mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"status\":\"ok\"}");
        } else {
          mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                        "{\"error\":\"Device not found\"}");
        }
      }
    }

    // POST /api/tuya/device/:id/control
    else if (mg_match(hm->uri, mg_str("/api/tuya/device/*/control"), NULL)) {
      const char *id_start = strstr(hm->uri.buf, "/api/tuya/device/");
      if (id_start) {
        id_start += 17;
        char device_id[TUYA_DEVICE_ID_MAX_LEN];
        int id_len = 0;
        while (id_start[id_len] && id_start[id_len] != '/' &&
               id_len < TUYA_DEVICE_ID_MAX_LEN - 1) {
          device_id[id_len] = id_start[id_len];
          id_len++;
        }
        device_id[id_len] = '\0';

        char action[32] = {0}, value[32] = {0};
        char *act = mg_json_get_str(hm->body, "$.action");
        char *val = mg_json_get_str(hm->body, "$.value");
        if (act) {
          strncpy(action, act, sizeof(action) - 1);
          free(act);
        }
        if (val) {
          strncpy(value, val, sizeof(value) - 1);
          free(val);
        }

        esp_err_t ret = -1;
        if (strcmp(action, "switch") == 0) {
          bool on = (strcmp(value, "on") == 0 || strcmp(value, "true") == 0);
          ret = tuya_mgr_set_switch(device_id, on);
        } else if (strcmp(action, "brightness") == 0) {
          ret = tuya_mgr_set_brightness(device_id, atoi(value));
        }

        if (ret == ESP_OK) {
          mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"status\":\"ok\"}");
        } else {
          mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                        "{\"error\":\"Control failed\"}");
        }
      }
    }

    // POST /api/tuya/device/:id/map_zone
    else if (mg_match(hm->uri, mg_str("/api/tuya/device/*/map_zone"), NULL)) {
      const char *id_start = strstr(hm->uri.buf, "/api/tuya/device/");
      if (id_start) {
        id_start += 17;
        char device_id[TUYA_DEVICE_ID_MAX_LEN];
        int id_len = 0;
        while (id_start[id_len] && id_start[id_len] != '/' &&
               id_len < TUYA_DEVICE_ID_MAX_LEN - 1) {
          device_id[id_len] = id_start[id_len];
          id_len++;
        }
        device_id[id_len] = '\0';

        char *zone_str = mg_json_get_str(hm->body, "$.zone_id");
        int zone_id = zone_str ? atoi(zone_str) : -1;
        if (zone_str)
          free(zone_str);

        if (tuya_mgr_map_to_zone(device_id, zone_id) == ESP_OK) {
          mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"status\":\"ok\"}");
        } else {
          mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                        "{\"error\":\"Invalid zone ID\"}");
        }
      }
    }

    // POST /api/tuya/device/:id/map_relay
    else if (mg_match(hm->uri, mg_str("/api/tuya/device/*/map_relay"), NULL)) {
      const char *id_start = strstr(hm->uri.buf, "/api/tuya/device/");
      if (id_start) {
        id_start += 17;
        char device_id[TUYA_DEVICE_ID_MAX_LEN];
        int id_len = 0;
        while (id_start[id_len] && id_start[id_len] != '/' &&
               id_len < TUYA_DEVICE_ID_MAX_LEN - 1) {
          device_id[id_len] = id_start[id_len];
          id_len++;
        }
        device_id[id_len] = '\0';

        char *relay_str = mg_json_get_str(hm->body, "$.relay_id");
        int relay_id = relay_str ? atoi(relay_str) : -1;
        if (relay_str)
          free(relay_str);

        if (tuya_mgr_map_to_relay(device_id, relay_id) == ESP_OK) {
          mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                        "{\"status\":\"ok\"}");
        } else {
          mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                        "{\"error\":\"Invalid relay ID\"}");
        }
      }
    }

    // GET /tuya.html - Tuya device management UI
    else if (mg_strcmp(hm->uri, mg_str("/tuya.html")) == 0) {
      struct mg_http_serve_opts opts = {.root_dir = "data"};
      mg_http_serve_file(c, hm, "data/tuya.html", &opts);
    }

    // Root path - serve index.html
    else if (mg_strcmp(hm->uri, mg_str("/")) == 0) {
      struct mg_http_serve_opts opts = {.root_dir = "data"};
      mg_http_serve_file(c, hm, "data/index.html", &opts);
    }

    // 23. Serve Static Files
    else {
      struct mg_http_serve_opts opts = {.root_dir = "data"};
      mg_http_serve_dir(c, hm, &opts);
    }
  }
}

// --- 6. TERMINAL SETTINGS ---
void set_conio_terminal_mode() {
  struct termios new_termios;
  tcgetattr(0, &new_termios);
  new_termios.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(0, TCSANOW, &new_termios);
}

void reset_terminal_mode() {
  struct termios orig_termios;
  tcgetattr(0, &orig_termios);
  orig_termios.c_lflag |= (ICANON | ECHO);
  tcsetattr(0, TCSANOW, &orig_termios);
}

// --- 7. MAIN LOOP ---
int main(void) {
  extern bool g_network_up;
  mg_log_set(0);
  mg_mgr_init(&mgr);

  set_conio_terminal_mode();
  storage_load_all();
  //    storage_debug_print();

  // Initialize security components
  printf("[SECURITY] Initializing security components...\n");
  if (!session_mgr_init()) {
    printf("[ERROR] Failed to initialize session manager\n");
  }
  if (!audit_log_init()) {
    printf("[ERROR] Failed to initialize audit log\n");
  }
  printf("[SECURITY] Security components initialized\n");

  // Initialize ESPHome API client (mock)
  printf("[ESPHOME] Initializing ESPHome API client...\n");
  extern void mock_esphome_init(void);
  mock_esphome_init();
  extern void esphome_zones_init(void);
  esphome_zones_init();
  printf("[ESPHOME] ESPHome API client initialized\n");

  // Initialize camera manager
  printf("[CAMERA] Initializing camera manager...\n");
  if (camera_mgr_init() != 0) {
    printf("[ERROR] Failed to initialize camera manager\n");
  } else {
    if (camera_mgr_load_config() != 0) {
      printf("[CAMERA] No camera config found or failed to load\n");
    } else {
      printf("[CAMERA] Camera manager initialized with %d camera(s)\n",
             camera_mgr_get_count());
    }
  };

  // Initialize Tuya manager (mock)
  printf("[TUYA] Initializing Tuya manager (mock)...\n");
  if (tuya_mgr_init(NULL, NULL) != ESP_OK) {
    printf("[ERROR] Failed to initialize Tuya manager\n");
  } else {
    printf("[TUYA] Tuya manager initialized with 3 mock devices\n");
  }

  engine_init();
  monitor_init();

  g_network_up = true;

  start_sentinel_mqtt();
  storage_debug_print();

  // Show ESPHome device list
  extern void mock_esphome_list_devices(void);
  mock_esphome_list_devices();

  printf("\n=== SENTINEL FULL MOCK SERVER ===\n");
  printf(">> Web UI (HTTP):  http://localhost:8000\n");
  printf(">> Web UI (HTTPS): https://localhost:8443 (self-signed cert)\n");
  printf("\n>> Matter Test: curl http://localhost:8000/api/matter/devices\n");
  printf(">> Matter Trigger: curl -X POST "
         "http://localhost:8000/api/matter/trigger -d '{\"zone_id\":33}'\n");
  printf("\n>> ESPHome Test: curl http://localhost:8000/api/esphome/devices\n");
  printf(">> ESPHome Trigger: curl -X POST "
         "http://localhost:8000/api/esphome/trigger -d "
         "'{\"entity_id\":\"basement_motion\",\"state\":true}'\n");
  printf(">> ESPHome Control: curl -X POST "
         "http://localhost:8000/api/esphome/control -d "
         "'{\"entity_id\":\"kitchen_light\",\"state\":true}'\n");
  printf("\n>> Camera Test: curl http://localhost:8000/api/cameras\n");
  printf(">> Camera Add: curl -X POST http://localhost:8000/api/camera/add -d "
         "'{\"hostname\":\"192.168.1.100\",\"port\":80,\"friendly_name\":"
         "\"Test Cam\",\"esphome_id\":\"camera\",\"enabled\":true}'\n");
  printf(">> Camera Snapshot: curl http://localhost:8000/api/camera/0/snapshot "
         "--output snapshot.jpg\n");

  // HTTP listener for compatibility
  mg_http_listen(&mgr, "http://0.0.0.0:8000", web_handler, NULL);

  // HTTPS listener on port 8443 (higher port for testing, pass non-NULL fn_data
  // for TLS init)
  mg_http_listen(&mgr, "https://0.0.0.0:8443", web_handler, (void *)1);

  // ESPHome Native API listener on port 6053 (reject ESPHome connections
  // gracefully)
  mg_listen(&mgr, "tcp://0.0.0.0:6053", esphome_reject_handler, NULL);

  int flags = fcntl(0, F_GETFL, 0);
  fcntl(0, F_SETFL, flags | O_NONBLOCK);

  char pin_buffer[16] = {0};
  int pin_idx = 0;
  uint64_t last_heartbeat = 0;
  uint64_t last_tick = 0;
  int nl_sync_counter = 0;

  // Track previous state to detect edges
  int last_pin_state[100];
  for (int i = 0; i < 100; i++)
    last_pin_state[i] = mock_gpio_pins[i];

  while (1) {
    mg_mgr_poll(&mgr, 10);
    uint64_t now = mg_now();

    // Poll for Zone Trips
    for (int i = 0; i < storage_get_zone_count(); i++) {
      int gpio = storage_get_zone(i)->gpio;
      if (gpio >= 0 && gpio < 100) {
        int curr = mock_gpio_pins[gpio];
        if (curr != last_pin_state[gpio]) {
          if (curr == 0) { // 0 = Violated in this mock
            printf("\n[MOCK] Zone %d (%s) GPIO %d TRIPPED -> Calling Engine\n",
                   i, storage_get_zone(i)->name, gpio);
            engine_process_zone_trip(i);
          }
          last_pin_state[gpio] = curr;
        }
      }
    }

    if (now - last_tick >= 50) {
      engine_tick();
      last_tick = now;
    }

    // Terminal Input
    char ch;
    if (read(0, &ch, 1) > 0) {
      if (ch == 10 || ch == 13) {
        pin_buffer[pin_idx] = '\0';
        if (pin_idx > 0)
          engine_handle_keypad_input(pin_buffer);
        pin_idx = 0;
        printf("\n");
      } else if (ch == 127 || ch == 8) {
        if (pin_idx > 0) {
          pin_idx--;
          printf("\b \b");
          fflush(stdout);
        }
      } else if (pin_idx < 15 && ch >= '0' && ch <= '9') {
        pin_buffer[pin_idx++] = ch;
        putchar('*');
        fflush(stdout);
      } else if (ch == 'c' || ch == 'C') {
        // Clear PIN memory when pressing 'C'
        memset(pin_buffer, 0, sizeof(pin_buffer)); // Wipe memory
        pin_idx = 0;                               // Reset counter
        printf("\nPIN Cleared. Enter PIN: ");      // Visual prompt
        fflush(stdout);
      }
    }

    // Noonlight Sync
    if (storage_get_config()->notify == 4 && ++nl_sync_counter >= 400) {
      nl_sync_counter = 0;
      noonlight_sync_task(storage_get_config());
    }

    // Heartbeat
    if (now - last_heartbeat >= 10000) {
      mqtt_publish_status();
      last_heartbeat = now;
    }
  }
  reset_terminal_mode();
  return 0;
}