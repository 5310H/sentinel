#!/bin/sh
################################################################################
# Wyze Floodlight v1 - Thingino Control Script
# Provides GPIO-based control for LED floodlight and PIR motion sensor
# 
# Installation: Copy to /usr/sbin/floodlight_ctl on Thingino device
# Usage: floodlight_ctl [on|off|auto|status]
#
# Version: 1.0
# Date: January 31, 2026
################################################################################

# Configuration
CONFIG_FILE="/etc/floodlight.conf"
STATE_FILE="/tmp/floodlight_state"
LOG_FILE="/var/log/floodlight.log"

# GPIO pin assignments (UPDATE THESE AFTER GPIO DISCOVERY)
# Run gpio_discover.sh first to find correct pins
FLOODLIGHT_GPIO="${FLOODLIGHT_GPIO:-52}"  # Default - must be configured
PIR_SENSOR_GPIO="${PIR_SENSOR_GPIO:-49}"   # Default - must be configured

# Auto mode settings
AUTO_MODE_TIMEOUT="${AUTO_MODE_TIMEOUT:-300}"  # 5 minutes default
LUX_THRESHOLD="${LUX_THRESHOLD:-100}"          # Turn on below this lux value

################################################################################
# Logging
################################################################################
log() {
    local level="$1"
    shift
    local msg="$*"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$timestamp] [$level] $msg" | tee -a "$LOG_FILE"
}

################################################################################
# GPIO Control Functions
################################################################################

# Export GPIO pin if not already exported
gpio_export() {
    local pin="$1"
    if [ ! -d "/sys/class/gpio/gpio${pin}" ]; then
        echo "$pin" > /sys/class/gpio/export 2>/dev/null
        sleep 0.2
    fi
}

# Set GPIO direction (in/out)
gpio_set_direction() {
    local pin="$1"
    local direction="$2"
    gpio_export "$pin"
    echo "$direction" > "/sys/class/gpio/gpio${pin}/direction"
}

# Set GPIO value (0/1)
gpio_set_value() {
    local pin="$1"
    local value="$2"
    echo "$value" > "/sys/class/gpio/gpio${pin}/value"
}

# Read GPIO value
gpio_get_value() {
    local pin="$1"
    gpio_export "$pin"
    cat "/sys/class/gpio/gpio${pin}/value"
}

################################################################################
# Floodlight Control
################################################################################

floodlight_on() {
    log "INFO" "Turning floodlight ON (GPIO $FLOODLIGHT_GPIO)"
    gpio_set_direction "$FLOODLIGHT_GPIO" "out"
    gpio_set_value "$FLOODLIGHT_GPIO" "1"
    echo "on" > "$STATE_FILE"
    echo "manual" >> "$STATE_FILE"
    
    # MQTT notification (if enabled)
    if command -v mosquitto_pub >/dev/null 2>&1; then
        mosquitto_pub -h "${MQTT_BROKER:-127.0.0.1}" \
                      -t "sentinel/floodlight/state" \
                      -m "ON" \
                      -u "${MQTT_USER}" \
                      -P "${MQTT_PASS}" 2>/dev/null
    fi
}

floodlight_off() {
    log "INFO" "Turning floodlight OFF (GPIO $FLOODLIGHT_GPIO)"
    gpio_set_direction "$FLOODLIGHT_GPIO" "out"
    gpio_set_value "$FLOODLIGHT_GPIO" "0"
    echo "off" > "$STATE_FILE"
    echo "manual" >> "$STATE_FILE"
    
    # MQTT notification
    if command -v mosquitto_pub >/dev/null 2>&1; then
        mosquitto_pub -h "${MQTT_BROKER:-127.0.0.1}" \
                      -t "sentinel/floodlight/state" \
                      -m "OFF" \
                      -u "${MQTT_USER}" \
                      -P "${MQTT_PASS}" 2>/dev/null
    fi
}

floodlight_status() {
    local current_value=$(gpio_get_value "$FLOODLIGHT_GPIO")
    local mode="unknown"
    
    if [ -f "$STATE_FILE" ]; then
        mode=$(tail -1 "$STATE_FILE")
    fi
    
    echo "Floodlight Status:"
    echo "  State: $([ "$current_value" = "1" ] && echo 'ON' || echo 'OFF')"
    echo "  Mode: $mode"
    echo "  GPIO: $FLOODLIGHT_GPIO"
    echo "  PIR GPIO: $PIR_SENSOR_GPIO"
    
    # Check PIR sensor
    if [ -d "/sys/class/gpio/gpio${PIR_SENSOR_GPIO}" ]; then
        local pir_value=$(gpio_get_value "$PIR_SENSOR_GPIO")
        echo "  PIR Motion: $([ "$pir_value" = "1" ] && echo 'DETECTED' || echo 'None')"
    fi
}

################################################################################
# Auto Mode (Motion-Activated)
################################################################################

floodlight_auto() {
    log "INFO" "Starting auto mode (motion-activated)"
    echo "auto" > "$STATE_FILE"
    
    # Setup PIR sensor as input with interrupt
    gpio_set_direction "$PIR_SENSOR_GPIO" "in"
    echo "both" > "/sys/class/gpio/gpio${PIR_SENSOR_GPIO}/edge"
    
    log "INFO" "Auto mode active - monitoring PIR sensor on GPIO $PIR_SENSOR_GPIO"
    
    # Background monitoring loop
    while true; do
        # Check if still in auto mode
        if [ ! -f "$STATE_FILE" ] || ! grep -q "auto" "$STATE_FILE"; then
            log "INFO" "Auto mode disabled, exiting monitor loop"
            break
        fi
        
        # Read PIR sensor
        local motion=$(gpio_get_value "$PIR_SENSOR_GPIO")
        local floodlight_state=$(gpio_get_value "$FLOODLIGHT_GPIO")
        
        # Get current lux (if light sensor available)
        local current_lux="999"
        if [ -f "/sys/devices/virtual/misc/sensor/lux" ]; then
            current_lux=$(cat /sys/devices/virtual/misc/sensor/lux)
        fi
        
        # Motion detected + dark environment
        if [ "$motion" = "1" ] && [ "$current_lux" -lt "$LUX_THRESHOLD" ]; then
            if [ "$floodlight_state" = "0" ]; then
                log "INFO" "Motion detected (lux: $current_lux) - turning ON"
                gpio_set_value "$FLOODLIGHT_GPIO" "1"
                
                # MQTT notification
                if command -v mosquitto_pub >/dev/null 2>&1; then
                    mosquitto_pub -h "${MQTT_BROKER:-127.0.0.1}" \
                                  -t "sentinel/floodlight/motion" \
                                  -m '{"motion": true, "lux": '$current_lux'}' \
                                  -u "${MQTT_USER}" \
                                  -P "${MQTT_PASS}" 2>/dev/null
                fi
            fi
            
            # Reset timeout
            echo "$(date +%s)" > /tmp/floodlight_motion_time
        fi
        
        # Check timeout - turn off if no motion for AUTO_MODE_TIMEOUT seconds
        if [ -f /tmp/floodlight_motion_time ] && [ "$floodlight_state" = "1" ]; then
            local last_motion=$(cat /tmp/floodlight_motion_time)
            local now=$(date +%s)
            local elapsed=$((now - last_motion))
            
            if [ "$elapsed" -gt "$AUTO_MODE_TIMEOUT" ]; then
                log "INFO" "No motion for ${AUTO_MODE_TIMEOUT}s - turning OFF"
                gpio_set_value "$FLOODLIGHT_GPIO" "0"
                rm -f /tmp/floodlight_motion_time
            fi
        fi
        
        sleep 0.5
    done
}

################################################################################
# Configuration Management
################################################################################

load_config() {
    if [ -f "$CONFIG_FILE" ]; then
        # shellcheck disable=SC1090
        . "$CONFIG_FILE"
        log "INFO" "Loaded config from $CONFIG_FILE"
    else
        log "WARN" "Config file not found, using defaults"
    fi
}

save_config() {
    cat > "$CONFIG_FILE" << EOF
# Wyze Floodlight v1 Configuration
# Generated: $(date)

# GPIO Pin Assignments
FLOODLIGHT_GPIO=$FLOODLIGHT_GPIO
PIR_SENSOR_GPIO=$PIR_SENSOR_GPIO

# Auto Mode Settings
AUTO_MODE_TIMEOUT=$AUTO_MODE_TIMEOUT  # seconds
LUX_THRESHOLD=$LUX_THRESHOLD          # turn on below this value

# MQTT Settings (for Sentinel integration)
MQTT_BROKER="${MQTT_BROKER:-192.168.1.50}"
MQTT_USER="${MQTT_USER:-sentinel}"
MQTT_PASS="${MQTT_PASS:-}"

# Enable/Disable Features
ENABLE_MQTT=${ENABLE_MQTT:-1}
ENABLE_AUTO_MODE=${ENABLE_AUTO_MODE:-1}
EOF
    log "INFO" "Saved config to $CONFIG_FILE"
}

################################################################################
# Main Command Handler
################################################################################

main() {
    local command="${1:-status}"
    
    # Load configuration
    load_config
    
    case "$command" in
        on)
            floodlight_on
            ;;
        off)
            floodlight_off
            ;;
        auto)
            # Kill existing auto mode process
            pkill -f "floodlight_ctl auto" 2>/dev/null
            
            # Start auto mode in background
            if [ "$2" = "daemon" ]; then
                floodlight_auto &
                echo $! > /tmp/floodlight_auto.pid
            else
                nohup "$0" auto daemon >> "$LOG_FILE" 2>&1 &
                echo "Auto mode started (PID: $!)"
            fi
            ;;
        status)
            floodlight_status
            ;;
        config)
            case "$2" in
                show)
                    cat "$CONFIG_FILE" 2>/dev/null || echo "No config file found"
                    ;;
                set)
                    # Set config value: floodlight_ctl config set FLOODLIGHT_GPIO 52
                    if [ -n "$3" ] && [ -n "$4" ]; then
                        export "$3"="$4"
                        save_config
                        echo "Set $3=$4"
                    else
                        echo "Usage: floodlight_ctl config set <KEY> <VALUE>"
                    fi
                    ;;
                *)
                    echo "Usage: floodlight_ctl config [show|set KEY VALUE]"
                    ;;
            esac
            ;;
        test)
            echo "Testing floodlight control..."
            echo "Turning ON for 3 seconds..."
            floodlight_on
            sleep 3
            echo "Turning OFF..."
            floodlight_off
            echo "Test complete"
            ;;
        help|--help|-h)
            cat << HELP
Wyze Floodlight v1 Control Script for Thingino

Usage: floodlight_ctl [COMMAND] [OPTIONS]

Commands:
  on              Turn floodlight ON (manual mode)
  off             Turn floodlight OFF (manual mode)
  auto            Enable auto mode (motion-activated lighting)
  status          Show current status and settings
  test            Test floodlight by turning on for 3 seconds
  config show     Display current configuration
  config set      Set configuration value
  help            Show this help message

Examples:
  floodlight_ctl on                           # Turn on
  floodlight_ctl auto                         # Enable motion detection
  floodlight_ctl config set FLOODLIGHT_GPIO 52  # Configure GPIO pin
  floodlight_ctl config set LUX_THRESHOLD 150   # Set light threshold

Configuration:
  Edit $CONFIG_FILE or use 'config set' command
  
GPIO Discovery:
  Run /usr/sbin/gpio_discover.sh first to identify correct GPIO pins
  
Integration:
  - MQTT: Publishes to sentinel/floodlight/state and sentinel/floodlight/motion
  - HTTP: Use Thingino's web UI to trigger via cron or webhook
  - Sentinel: Add as virtual relay or MQTT sensor

HELP
            ;;
        *)
            echo "Unknown command: $command"
            echo "Use 'floodlight_ctl help' for usage information"
            exit 1
            ;;
    esac
}

# Run main function
main "$@"
