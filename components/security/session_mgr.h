#ifndef SESSION_MGR_H
#define SESSION_MGR_H

/**
 * @file session_mgr.h
 * @brief JWT-based session token management
 * 
 * Provides secure session tokens to avoid re-authentication on every request.
 * Uses JWT (JSON Web Tokens) with HMAC-SHA256 signatures.
 * 
 * Security Features:
 * - 1-hour token expiration (configurable)
 * - IP address binding (optional, prevents token theft)
 * - Automatic cleanup of expired sessions
 * - NVS persistence (survives reboot)
 * - Revocation support (logout)
 * 
 * Token Format:
 * Header: {"alg":"HS256","typ":"JWT"}
 * Payload: {"sub":"username","iat":1234567890,"exp":1234571490,"ip":"192.168.1.100"}
 * Signature: HMAC-SHA256(header+payload, secret_key)
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define SESSION_TOKEN_MAX_LEN 384
#define SESSION_USERNAME_MAX_LEN 32
#define SESSION_IP_MAX_LEN 16
#define SESSION_MAX_ACTIVE 8         // Max concurrent sessions
#define SESSION_EXPIRY_SECONDS 3600  // 1 hour
#define SESSION_SECRET_KEY_LEN 32    // 256-bit secret key

/**
 * @brief Session data structure
 */
typedef struct {
    char token[SESSION_TOKEN_MAX_LEN];
    char username[SESSION_USERNAME_MAX_LEN];
    char ip_address[SESSION_IP_MAX_LEN];
    time_t issued_at;
    time_t expires_at;
    bool active;
} session_t;

/**
 * @brief Initialize session manager
 * 
 * Loads secret key from NVS or generates new one.
 * Loads active sessions from NVS.
 * Starts cleanup task to remove expired sessions.
 * 
 * @return true on success, false on error
 */
bool session_mgr_init(void);

/**
 * @brief Create new session token
 * 
 * Generates JWT token with username, timestamp, expiration.
 * Stores session in memory and NVS.
 * 
 * @param username Authenticated username
 * @param ip_address Client IP address (for binding, can be NULL)
 * @param out_token Output buffer for token (min SESSION_TOKEN_MAX_LEN bytes)
 * @return true on success, false if max sessions reached
 * 
 * @example
 * char token[SESSION_TOKEN_MAX_LEN];
 * if (session_create("john_doe", "192.168.1.100", token)) {
 *     // Return token to client: {"token": "eyJ0eXAi..."}
 * }
 */
bool session_create(const char *username, const char *ip_address, char *out_token);

/**
 * @brief Validate session token
 * 
 * Verifies JWT signature, checks expiration, optionally validates IP.
 * 
 * @param token JWT token from client (Authorization: Bearer <token>)
 * @param client_ip Client IP address (for validation, can be NULL to skip)
 * @param out_username Output buffer for username (min SESSION_USERNAME_MAX_LEN bytes)
 * @return true if valid, false if expired/invalid/revoked
 * 
 * @example
 * char username[SESSION_USERNAME_MAX_LEN];
 * if (session_validate(token, "192.168.1.100", username)) {
 *     // User authenticated, proceed with request
 * }
 */
bool session_validate(const char *token, const char *client_ip, char *out_username);

/**
 * @brief Revoke session (logout)
 * 
 * Marks session as inactive, removes from memory and NVS.
 * 
 * @param token Token to revoke
 * @return true if found and revoked, false if not found
 */
bool session_revoke(const char *token);

/**
 * @brief Revoke all sessions for a user
 * 
 * Used when password changed or user deleted.
 * 
 * @param username Username whose sessions to revoke
 * @return Number of sessions revoked
 */
int session_revoke_all_for_user(const char *username);

/**
 * @brief Clean up expired sessions
 * 
 * Called periodically by background task.
 * Removes sessions past expiration time.
 * 
 * @return Number of sessions cleaned up
 */
int session_cleanup_expired(void);

/**
 * @brief Get list of active sessions (for admin UI)
 * 
 * @param out_sessions Output array (min SESSION_MAX_ACTIVE elements)
 * @param max_sessions Size of output array
 * @return Number of active sessions copied
 */
int session_list_active(session_t *out_sessions, int max_sessions);

#endif // SESSION_MGR_H
