# Wyze Floodlight v1 - Sentinel Integration Script
# MQTT bridge for motion detection and floodlight control
#
# Installation:
# 1. Copy to /usr/bin/sentinel_integration.sh on Thingino device
# 2. Make executable: chmod +x /usr/bin/sentinel_integration.sh
# 3. Add to cron: echo "* * * * * /usr/bin/sentinel_integration.sh" >> /etc/crontabs/root
#
# Requirements:
# - mosquitto-clients package installed
# - floodlight_ctl configured and working
# - MQTT broker accessible

#!/bin/sh

# Sentinel MQTT Configuration
SENTINEL_MQTT_BROKER="${SENTINEL_MQTT_BROKER:-192.168.1.50}"
SENTINEL_MQTT_PORT="${SENTINEL_MQTT_PORT:-1883}"
SENTINEL_MQTT_USER="${SENTINEL_MQTT_USER:-floodlight}"
SENTINEL_MQTT_PASS="${SENTINEL_MQTT_PASS:-}"

# Device identification
DEVICE_ID="${DEVICE_ID:-wyze_floodlight_1}"
DEVICE_LOCATION="${DEVICE_LOCATION:-backyard}"

# Topics
TOPIC_MOTION="sentinel/motion/${DEVICE_LOCATION}"
TOPIC_FLOODLIGHT_CMD="sentinel/floodlight/${DEVICE_ID}/cmd"
TOPIC_FLOODLIGHT_STATE="sentinel/floodlight/${DEVICE_ID}/state"
TOPIC_FLOODLIGHT_ALARM="sentinel/alarm/snapshot"

# State files
LAST_MOTION_FILE="/tmp/last_motion_time"
SUBSCRIPTION_PID="/tmp/mqtt_subscription.pid"

################################################################################
# Motion Detection Publishing
################################################################################

# Monitor PIR sensor and publish to Sentinel
monitor_motion() {
    # Get PIR GPIO from config
    if [ -f /etc/floodlight.conf ]; then
        . /etc/floodlight.conf
    else
        PIR_SENSOR_GPIO="${PIR_SENSOR_GPIO:-49}"
    fi
    
    # Read PIR sensor value
    if [ ! -d "/sys/class/gpio/gpio${PIR_SENSOR_GPIO}" ]; then
        echo "$PIR_SENSOR_GPIO" > /sys/class/gpio/export 2>/dev/null
        sleep 0.2
        echo "in" > "/sys/class/gpio/gpio${PIR_SENSOR_GPIO}/direction"
    fi
    
    local motion_detected=$(cat "/sys/class/gpio/gpio${PIR_SENSOR_GPIO}/value" 2>/dev/null)
    
    # Only publish on state change
    local last_state=""
    if [ -f "$LAST_MOTION_FILE" ]; then
        last_state=$(cat "$LAST_MOTION_FILE")
    fi
    
    if [ "$motion_detected" = "1" ] && [ "$last_state" != "1" ]; then
        # Motion detected - publish to Sentinel
        local timestamp=$(date -Iseconds)
        local payload="{\"device\": \"$DEVICE_ID\", \"location\": \"$DEVICE_LOCATION\", \"motion\": true, \"timestamp\": \"$timestamp\"}"
        
        mosquitto_pub -h "$SENTINEL_MQTT_BROKER" \
                      -p "$SENTINEL_MQTT_PORT" \
                      -u "$SENTINEL_MQTT_USER" \
                      -P "$SENTINEL_MQTT_PASS" \
                      -t "$TOPIC_MOTION" \
                      -m "$payload" \
                      -q 1 \
                      2>/dev/null
        
        echo "1" > "$LAST_MOTION_FILE"
        logger -t sentinel_integration "Motion detected at $DEVICE_LOCATION"
        
    elif [ "$motion_detected" = "0" ] && [ "$last_state" = "1" ]; then
        # Motion cleared
        local timestamp=$(date -Iseconds)
        local payload="{\"device\": \"$DEVICE_ID\", \"location\": \"$DEVICE_LOCATION\", \"motion\": false, \"timestamp\": \"$timestamp\"}"
        
        mosquitto_pub -h "$SENTINEL_MQTT_BROKER" \
                      -p "$SENTINEL_MQTT_PORT" \
                      -u "$SENTINEL_MQTT_USER" \
                      -P "$SENTINEL_MQTT_PASS" \
                      -t "$TOPIC_MOTION" \
                      -m "$payload" \
                      -q 1 \
                      2>/dev/null
        
        echo "0" > "$LAST_MOTION_FILE"
    fi
}

################################################################################
# MQTT Command Subscription
################################################################################

# Subscribe to Sentinel commands
subscribe_commands() {
    # Kill existing subscription if running
    if [ -f "$SUBSCRIPTION_PID" ]; then
        local old_pid=$(cat "$SUBSCRIPTION_PID")
        kill "$old_pid" 2>/dev/null
    fi
    
    # Subscribe to floodlight commands
    mosquitto_sub -h "$SENTINEL_MQTT_BROKER" \
                  -p "$SENTINEL_MQTT_PORT" \
                  -u "$SENTINEL_MQTT_USER" \
                  -P "$SENTINEL_MQTT_PASS" \
                  -t "$TOPIC_FLOODLIGHT_CMD" \
                  -t "$TOPIC_FLOODLIGHT_ALARM" \
                  -v 2>/dev/null | while read -r topic message; do
        
        case "$topic" in
            "$TOPIC_FLOODLIGHT_CMD")
                # Handle floodlight control commands
                case "$message" in
                    on|ON)
                        /usr/sbin/floodlight_ctl on
                        publish_state "ON"
                        ;;
                    off|OFF)
                        /usr/sbin/floodlight_ctl off
                        publish_state "OFF"
                        ;;
                    auto|AUTO)
                        /usr/sbin/floodlight_ctl auto
                        publish_state "AUTO"
                        ;;
                esac
                ;;
                
            "$TOPIC_FLOODLIGHT_ALARM")
                # Sentinel alarm triggered - turn on floodlight and take snapshot
                logger -t sentinel_integration "Alarm triggered - activating floodlight"
                /usr/sbin/floodlight_ctl on
                
                # Take snapshot (if camera configured)
                sleep 1
                take_alarm_snapshot
                ;;
        esac
    done &
    
    echo $! > "$SUBSCRIPTION_PID"
}

################################################################################
# Floodlight State Publishing
################################################################################

publish_state() {
    local state="$1"
    local timestamp=$(date -Iseconds)
    local payload="{\"device\": \"$DEVICE_ID\", \"state\": \"$state\", \"timestamp\": \"$timestamp\"}"
    
    mosquitto_pub -h "$SENTINEL_MQTT_BROKER" \
                  -p "$SENTINEL_MQTT_PORT" \
                  -u "$SENTINEL_MQTT_USER" \
                  -P "$SENTINEL_MQTT_PASS" \
                  -t "$TOPIC_FLOODLIGHT_STATE" \
                  -m "$payload" \
                  -q 1 \
                  -r \
                  2>/dev/null
}

################################################################################
# Alarm Snapshot
################################################################################

take_alarm_snapshot() {
    # Take snapshot and upload to Sentinel
    local snapshot_file="/tmp/alarm_snapshot_$(date +%s).jpg"
    
    # Use curl to get snapshot from Thingino's own camera
    curl -s "http://127.0.0.1/image.jpg" -o "$snapshot_file"
    
    if [ -f "$snapshot_file" ] && [ -s "$snapshot_file" ]; then
        # Upload to Sentinel via HTTP POST
        curl -X POST \
             "http://${SENTINEL_MQTT_BROKER}/api/alarm/snapshot" \
             -F "device=$DEVICE_ID" \
             -F "location=$DEVICE_LOCATION" \
             -F "image=@$snapshot_file" \
             2>/dev/null
        
        logger -t sentinel_integration "Alarm snapshot uploaded"
        rm -f "$snapshot_file"
    fi
}

################################################################################
# Main Loop
################################################################################

main() {
    local mode="${1:-monitor}"
    
    case "$mode" in
        monitor)
            # Run motion monitoring (called by cron every minute)
            monitor_motion
            ;;
            
        daemon)
            # Run as persistent daemon (alternative to cron)
            logger -t sentinel_integration "Starting Sentinel integration daemon"
            
            # Start MQTT subscription in background
            subscribe_commands
            
            # Main monitoring loop
            while true; do
                monitor_motion
                sleep 1
            done
            ;;
            
        subscribe)
            # Start MQTT subscription only
            subscribe_commands
            wait
            ;;
            
        test)
            # Test MQTT connection
            echo "Testing MQTT connection to $SENTINEL_MQTT_BROKER..."
            if mosquitto_pub -h "$SENTINEL_MQTT_BROKER" \
                             -p "$SENTINEL_MQTT_PORT" \
                             -u "$SENTINEL_MQTT_USER" \
                             -P "$SENTINEL_MQTT_PASS" \
                             -t "sentinel/test" \
                             -m "test from $DEVICE_ID" \
                             2>/dev/null; then
                echo "✓ MQTT connection successful"
            else
                echo "✗ MQTT connection failed"
                exit 1
            fi
            
            echo "Testing motion sensor..."
            monitor_motion
            echo "✓ Motion sensor check complete (see $LAST_MOTION_FILE)"
            ;;
            
        help)
            cat << HELP
Sentinel Integration Script for Wyze Floodlight v1

Usage: sentinel_integration.sh [MODE]

Modes:
  monitor     Run motion detection once (for cron)
  daemon      Run as persistent background daemon
  subscribe   Subscribe to MQTT commands only
  test        Test MQTT connection and sensors
  help        Show this help

Configuration:
  Edit variables at top of script or set environment variables:
    SENTINEL_MQTT_BROKER=192.168.1.50
    DEVICE_ID=wyze_floodlight_1
    DEVICE_LOCATION=backyard

Installation (Cron):
  echo "* * * * * /usr/bin/sentinel_integration.sh monitor" >> /etc/crontabs/root

Installation (Daemon):
  /usr/bin/sentinel_integration.sh daemon &
  echo $! > /tmp/sentinel_daemon.pid

Sentinel MQTT Topics:
  - Motion: sentinel/motion/${DEVICE_LOCATION}
  - Commands: sentinel/floodlight/${DEVICE_ID}/cmd
  - State: sentinel/floodlight/${DEVICE_ID}/state
  - Alarm: sentinel/alarm/snapshot

HELP
            ;;
            
        *)
            echo "Unknown mode: $mode"
            echo "Use 'sentinel_integration.sh help' for usage"
            exit 1
            ;;
    esac
}

# Run main function
main "$@"
