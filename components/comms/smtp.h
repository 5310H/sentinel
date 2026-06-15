/**
 * @file smtp.h
 * @brief SMTP email notifications via libcurl
 * 
 * Sends alert and informational emails to configured recipients.
 * Supports multiple notification recipients (admin + users).
 * Uses TLS/SSL for secure SMTP connection.
 * 
 * Features:
 * - HTML formatted email templates
 * - Zone information in alert messages (name, type, timestamp)
 * - Multi-recipient delivery (admin email + user list)
 * - Retry on delivery failure
 * - Integration with dispatcher_alert() for alert routing
 * 
 * Note: Requires smtp_server, smtp_port, smtp_username, smtp_password, admin_email in config.json
 */

#ifndef SMTP_H
#define SMTP_H

#include "storage_mgr.h"

/**
 * @brief Send zone alarm notification to all configured recipients
 * 
 * Sends formatted alert email to:
 * - Admin email from config.admin_email
 * - All authorized users from users[] array
 * 
 * Email body includes:
 * - Zone name (z->name)
 * - Zone type (Door/Motion/Fire/etc.)
 * - Arm mode (Away/Stay/Night)
 * - Timestamp of trigger
 * - System location/identifier
 * 
 * Uses HTML template for professional appearance.
 * Non-blocking (queued via dispatcher_alert for async delivery).
 * 
 * @param config Configuration with SMTP server and credentials
 * @param users Array of authorized users to notify
 * @param user_count Number of users in array
 * @param z Zone that triggered alarm
 * 
 * @example
 * smtp_alert_all_contacts(&config, users, 5, &zones[0]);
 */
void smtp_alert_all_contacts(config_t *config, user_t *users, int user_count, zone_t *z);

/**
 * @brief Send system disarm confirmation email
 * 
 * Notifies admin and all users that alarm has been disarmed.
 * Includes:
 * - Disarm timestamp
 * - User who disarmed (if available)
 * - System state (Ready/Disarmed)
 * 
 * @param config Configuration with SMTP server and credentials
 * @param users Array of authorized users to notify
 * @param user_count Number of users in array
 * 
 * @example
 * smtp_send_cancellation(&config, users, user_count);
 */
void smtp_send_cancellation(config_t *config, user_t *users, int user_count);

/**
 * @brief Send custom email message
 * 
 * General-purpose email function for:
 * - Maintenance notifications
 * - Configuration changes
 * - System status reports
 * - Custom alerts
 * 
 * Sends to admin_email only.
 * 
 * @param config Configuration with SMTP server and credentials
 * @param subject Email subject line
 * @param body Email body (plain text or HTML)
 * 
 * @example
 * smtp_send_email(&config, "System Started", "Sentinel alarm initialized successfully");
 */
void smtp_send_email(config_t *config, const char *subject, const char *body);

#endif // SMTP_H