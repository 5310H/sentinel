#!/bin/bash
# Matter Integration Test Script

echo "=========================================="
echo "  Matter Controller Integration Test"
echo "=========================================="
echo ""

# Build test executable
echo "[1/3] Building with Matter mock..."
make clean > /dev/null 2>&1
make

if [ $? -ne 0 ]; then
    echo "❌ Build failed!"
    exit 1
fi

echo "✅ Build successful"
echo ""

# Create test scenario script
echo "[2/3] Creating test scenario..."

cat > matter_test_scenario.txt << 'EOF'
# Matter Test Scenario

# Initialize Matter
matter init

# List pre-configured devices
matter list

# Commission a new device
matter commission MT:Y.K9QAO00KNY0648G00 "Test Motion Sensor"

# Map to virtual zone 35
matter map zone 35

# Arm the system
arm away

# Simulate sensor trigger (should trigger alarm)
matter trigger 33

# Check alarm status
status

# Disarm
disarm 1234

# Test light control
matter light on
matter light brightness 127

# Trigger alarm response (all lights ON)
trigger alarm

EOF

echo "✅ Test scenario created"
echo ""

# Run interactive test
echo "[3/3] Running Matter integration test..."
echo ""
echo "Matter Mock Features:"
echo "  • 3 pre-configured devices (motion, light, contact)"
echo "  • Garage Motion → Zone 33"
echo "  • Front Door Light → Relay 8"
echo "  • Back Door Contact → Zone 34"
echo ""
echo "Test Commands:"
echo "  matter list              - Show all devices"
echo "  matter trigger <zone>    - Simulate sensor trigger"
echo "  matter light on/off      - Control lights"
echo "  matter commission <code> - Add device"
echo ""
echo "Standard Commands:"
echo "  arm away / arm stay      - Arm system"
echo "  disarm <pin>             - Disarm"
echo "  status                   - Show system state"
echo ""
echo "=========================================="
echo ""

# Start the test server
./sentinel_test

echo ""
echo "Test complete!"
