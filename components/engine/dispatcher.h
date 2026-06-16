#ifndef DISPATCHER_H
#define DISPATCHER_H

/**
 * @file dispatcher.h
 * @brief Alert routing and professional monitoring integration
 * 
 * Routes zone violations to configured notification services:
 * - Email (SMTP)
 * - Telegram Bot
 * - Noonlight (professional monitoring)
 * 
 * Implements "gatekeeper" logic to filter alerts by zone type
 * and system configuration before notification.
 */

#include "storage_mgr.h"

/**
 * @brief Process zone violation and route to notification services
 * 
 * Called when a zone is violated while armed. Implements gatekeeper logic:
 * - Checks if alert type monitoring is enabled (fire/police/medical/other)
 * - Filters based on storage_get_config()->notify setting
 * - Routes to appropriate notification method
 * - Prevents duplicate alerts for same zone
 * 
 * Alert routing:
 * - notify=1: None (silent alarm)
 * - notify=2: SMTP Email
 * - notify=3: Telegram Bot
 * - notify=4: Noonlight (professional monitoring)
 * 
 * @param z Violated zone (must be non-null)
 */
void dispatcher_alert(zone_t *z);

/**
 * @brief Send cancellation notice after system disarms
 * 
 * Called when alarm is disarmed during active alert.
 * Sends "all-clear" notices to all active notification channels
 * (professional monitoring, email, telegram).
 * 
 * Clears Noonlight dispatch IDs to allow new alarms.
 */
void dispatcher_cancel_alert(void);

#endif // DISPATCHER_H