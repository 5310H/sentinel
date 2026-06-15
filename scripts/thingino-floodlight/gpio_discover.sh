#!/bin/sh
################################################################################
# GPIO Discovery Script for Wyze Floodlight v1
# Identifies GPIO pins for LED floodlight and PIR motion sensor
#
# Installation: Run on Thingino device after flashing
# Usage: ./gpio_discover.sh
#
# Version: 1.0
# Date: January 31, 2026
################################################################################

echo "========================================"
echo "Wyze Floodlight v1 - GPIO Discovery"
echo "========================================"
echo ""

# Check if running on Thingino
if [ ! -f /etc/os-release ] || ! grep -q "Thingino" /etc/os-release; then
    echo "WARNING: This script is designed for Thingino firmware"
    echo "Press Ctrl+C to cancel or Enter to continue..."
    read
fi

################################################################################
# Step 1: Identify USB devices (floodlight may be USB-controlled)
################################################################################
echo "[Step 1] Scanning USB devices..."
echo ""
if command -v lsusb >/dev/null 2>&1; then
    lsusb
    echo ""
    echo "Look for unusual USB devices (LED controller, etc.)"
else
    echo "lsusb not available, skipping USB scan"
fi
echo ""

################################################################################
# Step 2: List all available GPIO pins
################################################################################
echo "[Step 2] Available GPIO pins..."
echo ""

GPIO_BASE="/sys/class/gpio"
if [ -d "$GPIO_BASE" ]; then
    # Try to export common GPIO ranges for Ingenic SoCs
    # T31 typically has GPIO 0-95
    echo "Attempting to export GPIO pins 0-95..."
    for gpio in $(seq 0 95); do
        if [ ! -d "$GPIO_BASE/gpio${gpio}" ]; then
            echo "$gpio" > "$GPIO_BASE/export" 2>/dev/null
        fi
    done
    
    sleep 1
    
    # List exported GPIOs
    echo ""
    echo "Exported GPIO pins:"
    ls -1 "$GPIO_BASE" | grep "gpio[0-9]" | sort -V
else
    echo "ERROR: GPIO subsystem not found at $GPIO_BASE"
    exit 1
fi
echo ""

################################################################################
# Step 3: Check for pre-configured GPIO pins
################################################################################
echo "[Step 3] Checking for pre-configured GPIOs..."
echo ""

# Check dmesg for GPIO-related messages
if command -v dmesg >/dev/null 2>&1; then
    echo "Recent GPIO activity in kernel log:"
    dmesg | grep -i gpio | tail -20
    echo ""
fi

# Check for existing GPIO exports in use
echo "Currently configured GPIO directions:"
for gpio_dir in "$GPIO_BASE"/gpio*/direction; do
    if [ -f "$gpio_dir" ]; then
        gpio_num=$(basename "$(dirname "$gpio_dir")" | sed 's/gpio//')
        direction=$(cat "$gpio_dir")
        value=$(cat "$(dirname "$gpio_dir")/value")
        echo "  GPIO $gpio_num: direction=$direction, value=$value"
    fi
done
echo ""

################################################################################
# Step 4: Test PIR Motion Sensor Detection
################################################################################
echo "[Step 4] Testing for PIR motion sensor..."
echo ""
echo "This will monitor GPIO pins for activity while you wave in front of the camera."
echo "Press Ctrl+C after 10 seconds or when you see activity."
echo ""
echo "Wave your hand in front of the PIR sensor NOW..."
sleep 2

# Monitor input GPIOs for changes
TEMP_DIR="/tmp/gpio_baseline"
mkdir -p "$TEMP_DIR"

# Take baseline reading
for gpio_path in "$GPIO_BASE"/gpio*/value; do
    if [ -f "$gpio_path" ]; then
        gpio_num=$(basename "$(dirname "$gpio_path")" | sed 's/gpio//')
        direction_file="$(dirname "$gpio_path")/direction"
        
        # Only check input pins
        if [ -f "$direction_file" ]; then
            direction=$(cat "$direction_file")
            if [ "$direction" = "in" ]; then
                value=$(cat "$gpio_path")
                echo "$value" > "$TEMP_DIR/gpio${gpio_num}_baseline"
            fi
        fi
    fi
done

echo "Monitoring for 10 seconds..."
for i in $(seq 1 20); do
    for gpio_path in "$GPIO_BASE"/gpio*/value; do
        if [ -f "$gpio_path" ]; then
            gpio_num=$(basename "$(dirname "$gpio_path")" | sed 's/gpio//')
            baseline_file="$TEMP_DIR/gpio${gpio_num}_baseline"
            
            if [ -f "$baseline_file" ]; then
                baseline=$(cat "$baseline_file")
                current=$(cat "$gpio_path" 2>/dev/null)
                
                if [ "$current" != "$baseline" ]; then
                    echo ">>> ACTIVITY DETECTED on GPIO $gpio_num: $baseline -> $current <<<"
                    echo "$gpio_num" >> "$TEMP_DIR/active_gpios"
                fi
            fi
        fi
    done
    sleep 0.5
done

echo ""
if [ -f "$TEMP_DIR/active_gpios" ]; then
    echo "GPIOs with detected activity (likely PIR sensor):"
    sort -u "$TEMP_DIR/active_gpios" | while read gpio; do
        echo "  - GPIO $gpio"
    done
    echo ""
    echo "Recommended PIR_SENSOR_GPIO: $(head -1 "$TEMP_DIR/active_gpios")"
else
    echo "No GPIO activity detected. Try again with more motion."
fi
rm -rf "$TEMP_DIR"
echo ""

################################################################################
# Step 5: Test Floodlight LED Control
################################################################################
echo "[Step 5] Testing for floodlight LED control..."
echo ""
echo "This will attempt to toggle output GPIOs to find the floodlight control pin."
echo "Watch the floodlight LEDs for any changes."
echo ""
read -p "Press Enter to start LED test (Ctrl+C to skip)..." dummy

# Find output GPIOs
echo "Testing output GPIO pins..."
for gpio_path in "$GPIO_BASE"/gpio*/direction; do
    if [ -f "$gpio_path" ]; then
        gpio_num=$(basename "$(dirname "$gpio_path")" | sed 's/gpio//')
        direction=$(cat "$gpio_path")
        
        # Test output pins
        if [ "$direction" = "out" ]; then
            echo -n "Testing GPIO $gpio_num... "
            
            # Toggle on
            echo "1" > "$GPIO_BASE/gpio${gpio_num}/value" 2>/dev/null
            sleep 0.5
            
            echo "ON - Check if floodlight changed"
            sleep 1
            
            # Toggle off
            echo "0" > "$GPIO_BASE/gpio${gpio_num}/value" 2>/dev/null
            sleep 0.5
            echo "OFF"
            
            read -p "Did the floodlight turn ON? (y/n): " answer
            if [ "$answer" = "y" ] || [ "$answer" = "Y" ]; then
                echo ""
                echo ">>> FOUND FLOODLIGHT GPIO: $gpio_num <<<"
                echo "Recommended FLOODLIGHT_GPIO: $gpio_num"
                FOUND_FLOODLIGHT_GPIO="$gpio_num"
                break
            fi
        fi
    fi
done

echo ""

################################################################################
# Step 6: Check for spotlight_ctl or similar utilities
################################################################################
echo "[Step 6] Checking for existing control utilities..."
echo ""

if [ -f "/usr/sbin/spotlight_ctl" ]; then
    echo "Found spotlight_ctl - may have similar floodlight control"
    echo "Analyzing..."
    head -50 /usr/sbin/spotlight_ctl
elif [ -f "/usr/sbin/floodlight_ctl" ]; then
    echo "Found floodlight_ctl utility!"
    which floodlight_ctl
else
    echo "No pre-existing control utilities found"
fi
echo ""

################################################################################
# Step 7: Generate configuration file
################################################################################
echo "[Step 7] Generating configuration..."
echo ""

if [ -n "$FOUND_FLOODLIGHT_GPIO" ]; then
    cat > /tmp/floodlight_discovered.conf << EOF
# Auto-discovered GPIO configuration
# Generated: $(date)

FLOODLIGHT_GPIO=$FOUND_FLOODLIGHT_GPIO
PIR_SENSOR_GPIO=${FOUND_PIR_GPIO:-49}  # Update if different
EOF
    
    echo "Configuration saved to /tmp/floodlight_discovered.conf"
    cat /tmp/floodlight_discovered.conf
    echo ""
    echo "Copy this to /etc/floodlight.conf to use with floodlight_control.sh"
else
    echo "Manual GPIO configuration required"
    echo "Possible GPIO ranges to test manually:"
    echo "  - Output GPIOs: 48-63 (common for LED controls)"
    echo "  - Input GPIOs: 40-55 (common for sensors)"
fi

echo ""
echo "========================================"
echo "Discovery Complete"
echo "========================================"
echo ""
echo "Next steps:"
echo "1. Note the discovered GPIO pins above"
echo "2. Edit /etc/floodlight.conf with correct GPIO values"
echo "3. Install floodlight_control.sh to /usr/sbin/floodlight_ctl"
echo "4. Test with: floodlight_ctl test"
echo ""
echo "If automatic discovery failed:"
echo "  - Join Thingino Discord: https://discord.gg/xDmqS944zr"
echo "  - Ask about Wyze Floodlight v1 GPIO mapping"
echo "  - Share output of this script for community help"
echo ""
