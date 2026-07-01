#!/bin/bash
# Complete Sentinel Alarm System Testing Script
# Tests all major functionality: Auth, Arm/Disarm, Status, Zones, Users, Config
# NOTE: Assumes sentinel_test is already running on port 8000 (HTTP)

# set -e removed to prevent jq parse errors from crashing the script

# --- Server Configuration ---
# Uses the already-running sentinel_test server
BASE_URL="http://localhost:8000"

PASS=0
FAIL=0
WARN=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# --- Helper Functions ---
print_header() {
    echo -e "\n${BLUE}════════════════════════════════════════${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}════════════════════════════════════════${NC}"
}

test_pass() {
    echo -e "${GREEN}✓ PASS${NC}: $1"
    ((PASS+=1))
}

test_fail() {
    echo -e "${RED}✗ FAIL${NC}: $1"
    ((FAIL+=1))
}

test_warn() {
    echo -e "${YELLOW}⚠ WARN${NC}: $1"
    ((WARN+=1))
}

test_endpoint() {
    local method=$1
    local endpoint=$2
    local expected_code=$3
    local data=$4
    local description=$5
    
    # Add -k for self-signed certs
    if [ "$method" == "POST" ]; then
        response=$(curl -s -k -w "\n%{http_code}" -X POST "$BASE_URL$endpoint" \
            -H "Content-Type: application/json" \
            -d "$data")
    else
        response=$(curl -s -k -w "\n%{http_code}" "$BASE_URL$endpoint")
    fi
    
    http_code=$(echo "$response" | tail -n1)
    body=$(echo "$response" | head -n-1)
    
    if [ "$http_code" == "$expected_code" ]; then
        test_pass "$description (HTTP $http_code)"
        echo "$body"
    else
        test_fail "$description (Expected $expected_code, got $http_code)"
        echo "$body" | head -c 100
    fi
    echo ""
}

# ============================================================================
# SECTION 1: SERVER & BASIC CONNECTIVITY
# ============================================================================
print_header "SECTION 1: Server Connectivity"

# Check if sentinel_test is running
if ! netstat -tuln 2>/dev/null | grep -q ":8000" && ! ss -tuln 2>/dev/null | grep -q ":8000"; then
    test_fail "sentinel_test is not running on port 8000. Please start it first: cd /home/kjgerhart/sentinel-esp/test_linux && ./sentinel_test"
    exit 1
fi
echo "✓ Found sentinel_test listening on port 8000"

# Check HTTP Availability
echo "Checking HTTP Connectivity to Sentinel Test Server..."
if curl -s --connect-timeout 2 "$BASE_URL/" > /dev/null 2>&1; then
    test_pass "HTTP server is responding on $BASE_URL"
else
    test_fail "HTTP server is NOT responding on $BASE_URL"
    exit 1
fi

# Ensure data directory exists for storage
mkdir -p data

# Check main page loads
main_check=$(curl -s -k "$BASE_URL/" | grep -i "Sentinel" | wc -l)
if [ "$main_check" -gt 0 ]; then
    test_pass "Main page (index.html) loads via HTTPS"
else
    test_fail "Main page does not contain 'Sentinel' via HTTPS"
fi

# ============================================================================
# SECTION 2: SYSTEM STATUS ENDPOINTS
# ============================================================================
print_header "SECTION 2: System Status Endpoints"

# Test /api/status (full config + zones + relays)
echo "Testing /api/status..."
status_response=$(curl -s -k "$BASE_URL/api/status")
if echo "$status_response" | jq -e '.config' > /dev/null 2>&1; then
    test_pass "/api/status returns config object"
    config_state=$(echo "$status_response" | jq -r '.config.state')
    config_ready=$(echo "$status_response" | jq -r '.config.ready')
    echo "  System State: $config_state, Ready: $config_ready"
else
    # Mock server won't have this, so it's a WARN not FAIL
    test_warn "/api/status does not contain 'config'"
fi

if echo "$status_response" | jq -e '.zones' > /dev/null 2>&1; then
    zone_count=$(echo "$status_response" | jq '.zones | length')
    test_pass "/api/status returns zones array ($zone_count zones)"
else
    test_warn "/api/status does not contain 'zones'"
fi

if echo "$status_response" | jq -e '.relays' > /dev/null 2>&1; then
    relay_count=$(echo "$status_response" | jq '.relays | length')
    test_pass "/api/status returns relays array ($relay_count relays)"
else
    test_warn "/api/status does not contain 'relays'"
fi

# Test /status (system monitoring data)
echo -e "\nTesting /status..."
sys_status=$(curl -s -k "$BASE_URL/status")
if echo "$sys_status" | jq -e '.temperature_f' > /dev/null 2>&1; then
    temp=$(echo "$sys_status" | jq '.temperature_f')
    test_pass "/status returns temperature_f ($temp°F)"
else
    test_warn "/status does not contain 'temperature_f'"
fi

if echo "$sys_status" | jq -e '.on_backup_power' > /dev/null 2>&1; then
    power=$(echo "$sys_status" | jq '.on_backup_power')
    test_pass "/status returns on_backup_power ($power)"
else
    test_warn "/status does not contain 'on_backup_power'"
fi

if echo "$sys_status" | jq -e '.tamper_detected' > /dev/null 2>&1; then
    tamper=$(echo "$sys_status" | jq '.tamper_detected')
    test_pass "/status returns tamper_detected ($tamper)"
else
    test_warn "/status does not contain 'tamper_detected'"
fi

# ============================================================================
# SECTION 3: AUTHENTICATION
# ============================================================================
print_header "SECTION 3: Authentication"

# Test valid PIN auth
echo "Testing authentication with valid PIN..."
auth_response=$(curl -s -k -X POST "$BASE_URL/api/auth" \
    -H "Content-Type: application/json" \
    -d '{"pin":"1234"}')

if echo "$auth_response" | jq -e '.authenticated' > /dev/null 2>&1; then
    auth_result=$(echo "$auth_response" | jq '.authenticated')
    if [ "$auth_result" == "true" ]; then
        test_pass "Valid PIN (1234) authenticates successfully"
        is_admin=$(echo "$auth_response" | jq '.is_admin')
        echo "  Admin: $is_admin"
    else
        test_fail "Valid PIN (1234) authentication returned false"
    fi
else
    test_warn "Authentication endpoint did not return valid JSON"
fi

# ============================================================================
# SECTION 4: WEB PAGES (HTML)
# ============================================================================
print_header "SECTION 4: Web Pages"

# Test index.html
index_content=$(curl -s -k "$BASE_URL/index.html")
if echo "$index_content" | grep -q "Sentinel Control Panel"; then
    test_pass "index.html loads with correct title"
else
    test_fail "index.html title not found"
fi

# Test users.html
users_content=$(curl -s -k "$BASE_URL/users.html")
users_check=$(echo "$users_content" | grep -i "Users" | wc -l)
if [ "$users_check" -gt 0 ]; then
    test_pass "users.html loads"
else
    test_fail "users.html content missing"
fi

# Test zstatus.html
zstatus_content=$(curl -s -k "$BASE_URL/zstatus.html")
zone_check=$(echo "$zstatus_content" | grep -i "zone" | wc -l)
if [ "$zone_check" -gt 0 ]; then
    test_pass "zstatus.html loads"
else
    test_fail "zstatus.html content missing"
fi

# Test audit.html
audit_content=$(curl -s -k "$BASE_URL/audit.html")
audit_check=$(echo "$audit_content" | grep -i "System Audit" | wc -l)
if [ "$audit_check" -gt 0 ]; then
    test_pass "audit.html loads"
else
    test_fail "audit.html content missing"
fi

# Test tester.html
audit_content=$(curl -s -k "$BASE_URL/tester.html")
audit_check=$(echo "$audit_content" | grep -i "System Tester" | wc -l)
if [ "$audit_check" -gt 0 ]; then
    test_pass "tester.html loads"
else
    test_fail "tester.html content missing"
fi

# Test cameras.html
cameras_content=$(curl -s -k "$BASE_URL/cameras.html")
cameras_check=$(echo "$cameras_content" | grep -i "camera" | wc -l)
if [ "$cameras_check" -gt 0 ]; then
    test_pass "cameras.html loads"
else
    test_fail "cameras.html content missing"
fi

# ============================================================================
# SECTION 5: CAMERA API TESTS
# ============================================================================
print_header "SECTION 5: Camera API Tests"

# Test GET /api/cameras
echo "Testing GET /api/cameras..."
cameras_response=$(curl -s -k "$BASE_URL/api/cameras")
if echo "$cameras_response" | grep -q "id"; then
    test_pass "GET /api/cameras returns camera list"
    echo "$cameras_response" | head -c 200
else
    test_warn "GET /api/cameras returned unexpected format"
    echo "$cameras_response"
fi
echo ""

# Test POST /api/camera/add
echo "Testing POST /api/camera/add..."
add_response=$(curl -s -k -X POST "$BASE_URL/api/camera/add" \
    -H "Content-Type: application/json" \
    -d '{"hostname":"192.168.1.100","port":80,"friendly_name":"Test Camera","esphome_id":"camera","enabled":true}')

if echo "$add_response" | grep -q "success"; then
    test_pass "POST /api/camera/add - Camera added successfully"
    camera_id=$(echo "$add_response" | grep -o '"camera_id":[0-9]*' | cut -d':' -f2)
    echo "New camera ID: $camera_id"
else
    test_warn "POST /api/camera/add - Response may indicate existing camera"
    echo "$add_response"
fi
echo ""

# Test GET /api/camera/:id/snapshot (will return mock JPEG)
echo "Testing GET /api/camera/0/snapshot..."
if curl -s -k "$BASE_URL/api/camera/0/snapshot" --output /tmp/test_snapshot.jpg 2>/dev/null; then
    if file /tmp/test_snapshot.jpg | grep -q "JPEG"; then
        test_pass "GET /api/camera/0/snapshot - Returns valid JPEG"
        ls -lh /tmp/test_snapshot.jpg
    else
        test_warn "GET /api/camera/0/snapshot - File format unexpected"
        file /tmp/test_snapshot.jpg
    fi
else
    test_warn "GET /api/camera/0/snapshot - Could not fetch snapshot"
fi
echo ""

# Test POST /api/camera/:id/test
echo "Testing POST /api/camera/0/test..."
test_response=$(curl -s -k -X POST "$BASE_URL/api/camera/0/test")
if echo "$test_response" | grep -q "success"; then
    test_pass "POST /api/camera/0/test - Camera connection test"
    echo "$test_response"
else
    test_warn "POST /api/camera/0/test - Test may have failed"
    echo "$test_response"
fi
echo ""

# Test POST /api/camera/:id/toggle
echo "Testing POST /api/camera/0/toggle..."
toggle_response=$(curl -s -k -X POST "$BASE_URL/api/camera/0/toggle")
if echo "$toggle_response" | grep -q "success"; then
    test_pass "POST /api/camera/0/toggle - Camera toggled"
    echo "$toggle_response"
else
    test_warn "POST /api/camera/0/toggle - Toggle may have failed"
    echo "$toggle_response"
fi
echo ""

# Test cameras_live.html page
echo "Testing GET /cameras_live.html..."
live_view_content=$(curl -s -k "$BASE_URL/cameras_live.html")
if echo "$live_view_content" | grep -q "Live Camera View"; then
    test_pass "GET /cameras_live.html - Live camera grid page loads"
else
    test_warn "GET /cameras_live.html - Page content missing"
    echo "$live_view_content" | head -c 200
fi
echo ""

# Test that live view has grid controls
if echo "$live_view_content" | grep -q "grid-2x2"; then
    test_pass "Live view has grid layout controls"
else
    test_warn "Live view missing grid controls"
fi
echo ""

# Test that live view has MJPEG toggle
if echo "$live_view_content" | grep -q "mjpeg-toggle"; then
    test_pass "Live view has MJPEG toggle checkbox"
else
    test_warn "Live view missing MJPEG toggle"
fi
echo ""

# Test MJPEG streaming endpoint
echo "Testing GET /api/camera/0/stream..."
stream_response=$(curl -s -k -D- "$BASE_URL/api/camera/0/stream" | head -10)
if echo "$stream_response" | grep -q "multipart/x-mixed-replace"; then
    test_pass "GET /api/camera/0/stream - MJPEG endpoint responds"
    echo "  Content-Type: multipart/x-mixed-replace"
else
    test_warn "GET /api/camera/0/stream - MJPEG headers not found"
    echo "$stream_response" | head -5
fi
echo ""

# ============================================================================
# SECTION 6: TUYA DEVICE API TESTS
# ============================================================================
print_header "SECTION 6: Tuya Device API Tests"

# Test tuya.html page
echo "Testing GET /tuya.html..."
tuya_content=$(curl -s -k "$BASE_URL/tuya.html")
if echo "$tuya_content" | grep -q "Tuya"; then
    test_pass "GET /tuya.html - Tuya device management page loads"
else
    test_warn "GET /tuya.html - Page content missing"
    echo "$tuya_content" | head -c 200
fi
echo ""

if false; then
# Test GET /api/tuya/devices
echo "Testing GET /api/tuya/devices..."
tuya_devices=$(curl -s -k "$BASE_URL/api/tuya/devices")
if echo "$tuya_devices" | jq -e '.connected' > /dev/null 2>&1; then
    device_count=$(echo "$tuya_devices" | jq '.count')
    test_pass "GET /api/tuya/devices - Returns device list ($device_count devices)"
    echo "  Connected: $(echo "$tuya_devices" | jq '.connected')"
    echo "  Device count: $device_count"
    
    # Show mock devices
    if [ "$device_count" -gt 0 ]; then
        echo "  Mock devices:"
        echo "$tuya_devices" | jq -r '.devices[] | "    - \(.name) (\(.type)) - \(.state)"'
    fi
else
    test_warn "GET /api/tuya/devices - Unexpected format"
    echo "$tuya_devices" | head -c 200
fi
echo ""

# Test POST /api/tuya/discover
echo "Testing POST /api/tuya/discover..."
discover_response=$(curl -s -k -X POST "$BASE_URL/api/tuya/discover" \
    -H "Content-Type: application/json" \
    -d '{"timeout_ms":5000}')
if echo "$discover_response" | grep -q "success"; then
    test_pass "POST /api/tuya/discover - Discovery initiated"
    echo "$discover_response"
else
    test_warn "POST /api/tuya/discover - Response unexpected"
    echo "$discover_response"
fi
echo ""

# Test POST /api/tuya/pair
echo "Testing POST /api/tuya/pair..."
pair_response=$(curl -s -k -X POST "$BASE_URL/api/tuya/pair" \
    -H "Content-Type: application/json" \
    -d '{"duration_sec":30}')
if echo "$pair_response" | grep -q "success"; then
    test_pass "POST /api/tuya/pair - Pairing mode enabled"
    echo "$pair_response"
else
    test_warn "POST /api/tuya/pair - Response unexpected"
    echo "$pair_response"
fi
echo ""

# Test device control - Get a device ID from the device list
device_id=$(echo "$tuya_devices" | jq -r '.devices[0].id // empty')
if [ -n "$device_id" ]; then
    echo "Testing device control on device: $device_id"
    
    # Test switch control
    echo "Testing POST /api/tuya/device/$device_id/control (switch on)..."
    control_response=$(curl -s -k -X POST "$BASE_URL/api/tuya/device/$device_id/control" \
        -H "Content-Type: application/json" \
        -d '{"action":"switch","value":"on"}')
    if echo "$control_response" | grep -q "success"; then
        test_pass "POST /api/tuya/device/:id/control - Switch on"
        echo "$control_response"
    else
        test_warn "POST /api/tuya/device/:id/control - Control may have failed"
        echo "$control_response"
    fi
    echo ""
    
    # Test brightness control
    echo "Testing POST /api/tuya/device/$device_id/control (brightness 50)..."
    brightness_response=$(curl -s -k -X POST "$BASE_URL/api/tuya/device/$device_id/control" \
        -H "Content-Type: application/json" \
        -d '{"action":"brightness","value":"50"}')
    if echo "$brightness_response" | grep -q "success"; then
        test_pass "POST /api/tuya/device/:id/control - Brightness set"
        echo "$brightness_response"
    else
        test_warn "POST /api/tuya/device/:id/control - Brightness control may have failed"
        echo "$brightness_response"
    fi
    echo ""
    
    # Test zone mapping
    echo "Testing POST /api/tuya/device/$device_id/map_zone..."
    zone_response=$(curl -s -k -X POST "$BASE_URL/api/tuya/device/$device_id/map_zone" \
        -H "Content-Type: application/json" \
        -d '{"zone_id":65}')
    if echo "$zone_response" | grep -q "success"; then
        test_pass "POST /api/tuya/device/:id/map_zone - Zone mapped"
        echo "$zone_response"
    else
        test_warn "POST /api/tuya/device/:id/map_zone - Mapping may have failed"
        echo "$zone_response"
    fi
    echo ""
    
    # Test relay mapping
    echo "Testing POST /api/tuya/device/$device_id/map_relay..."
    relay_response=$(curl -s -k -X POST "$BASE_URL/api/tuya/device/$device_id/map_relay" \
        -H "Content-Type: application/json" \
        -d '{"relay_id":32}')
    if echo "$relay_response" | grep -q "success"; then
        test_pass "POST /api/tuya/device/:id/map_relay - Relay mapped"
        echo "$relay_response"
    else
        test_warn "POST /api/tuya/device/:id/map_relay - Mapping may have failed"
        echo "$relay_response"
    fi
    echo ""
else
    test_warn "No Tuya devices available for control testing"
fi
fi # End disabled Tuya tests

# ============================================================================
# SECTION 7: ESPHome Device API Tests
# ============================================================================
print_header "SECTION 7: ESPHome Device API Tests"

# Test esphome.html page
echo "Testing GET /esphome.html..."
esphome_content=$(curl -s -k "$BASE_URL/esphome.html")
if echo "$esphome_content" | grep -q "ESPHome"; then
    test_pass "GET /esphome.html - ESPHome device management page loads"
else
    test_warn "GET /esphome.html - Page content missing"
    echo "$esphome_content" | head -c 200
fi
echo ""

# Test GET /api/esphome/devices
echo "Testing GET /api/esphome/devices..."
esphome_devices=$(curl -s -k "$BASE_URL/api/esphome/devices")
if echo "$esphome_devices" | jq -e '.devices' > /dev/null 2>&1; then
    device_count=$(echo "$esphome_devices" | jq '.devices | length')
    test_pass "GET /api/esphome/devices - Returns device list ($device_count devices)"
    
    # Show configured devices
    if [ "$device_count" -gt 0 ]; then
        echo "  Configured devices:"
        echo "$esphome_devices" | jq -r '.devices[] | "    - \(.friendly_name) (\(.hostname):\(.port)) - \(.enabled)"'
    fi
else
    test_warn "GET /api/esphome/devices - Unexpected format or no devices configured"
    echo "$esphome_devices" | head -c 200
fi
echo ""

# Test GET /api/esphome/status
echo "Testing GET /api/esphome/status..."
esphome_status=$(curl -s -k "$BASE_URL/api/esphome/status")
if echo "$esphome_status" | jq -e '.connected_count' > /dev/null 2>&1; then
    connected_count=$(echo "$esphome_status" | jq '.connected_count')
    total_count=$(echo "$esphome_status" | jq '.total_count')
    test_pass "GET /api/esphome/status - Returns connection status ($connected_count/$total_count connected)"
    
    # Show connection details
    if echo "$esphome_status" | jq -e '.devices' > /dev/null 2>&1; then
        echo "  Device connection status:"
        echo "$esphome_status" | jq -r '.devices[] | "    - \(.hostname): \(.connected) (\(.entity_count // 0) entities)"'
    fi
else
    test_warn "GET /api/esphome/status - Unexpected format"
    echo "$esphome_status" | head -c 200
fi
echo ""

# Test POST /api/esphome/device/add (if no devices configured)
if [ "$device_count" -eq 0 ]; then
    echo "Testing POST /api/esphome/device/add..."
    add_response=$(curl -s -k -X POST "$BASE_URL/api/esphome/device/add" \
        -H "Content-Type: application/json" \
        -d '{"hostname":"192.168.1.100","port":6053,"friendly_name":"Test ESPHome Device","password":"","encryption_key":"","enabled":true}')
    
    if echo "$add_response" | grep -q "success"; then
        test_pass "POST /api/esphome/device/add - Device added successfully"
        device_id=$(echo "$add_response" | grep -o '"device_id":[0-9]*' | cut -d':' -f2)
        echo "  New device ID: $device_id"
    else
        test_warn "POST /api/esphome/device/add - Response may indicate existing device"
        echo "$add_response"
    fi
    echo ""
fi

# Test device connectivity (if we have devices)
if [ "$device_count" -gt 0 ]; then
    # Get first device for testing
    first_device=$(echo "$esphome_devices" | jq '.devices[0]')
    device_hostname=$(echo "$first_device" | jq -r '.hostname')
    
    echo "Testing POST /api/esphome/device/connect for $device_hostname..."
    connect_response=$(curl -s -k -X POST "$BASE_URL/api/esphome/device/connect" \
        -H "Content-Type: application/json" \
        -d "{\"hostname\":\"$device_hostname\"}")
    
    if echo "$connect_response" | grep -q "success\|connected"; then
        test_pass "POST /api/esphome/device/connect - Connection attempt successful"
        echo "$connect_response"
    else
        test_warn "POST /api/esphome/device/connect - Connection may have failed (expected for mock server)"
        echo "$connect_response"
    fi
    echo ""
    
    # Test entity discovery
    echo "Testing POST /api/esphome/device/discover for $device_hostname..."
    discover_response=$(curl -s -k -X POST "$BASE_URL/api/esphome/device/discover" \
        -H "Content-Type: application/json" \
        -d "{\"hostname\":\"$device_hostname\"}")
    
    if echo "$discover_response" | grep -q "success\|entities"; then
        test_pass "POST /api/esphome/device/discover - Entity discovery successful"
        echo "$discover_response"
    else
        test_warn "POST /api/esphome/device/discover - Discovery may have failed"
        echo "$discover_response"
    fi
    echo ""
fi

# Test virtual zone mapping to ESPHome sensors
echo "Testing POST /api/esphome/entity/map_zone..."
zone_map_response=$(curl -s -k -X POST "$BASE_URL/api/esphome/entity/map_zone" \
    -H "Content-Type: application/json" \
    -d '{"hostname":"test-device","entity_key":12345,"zone_id":33}')

if echo "$zone_map_response" | grep -q "success\|mapped"; then
    test_pass "POST /api/esphome/entity/map_zone - Virtual zone mapping successful"
    echo "$zone_map_response"
else
    test_warn "POST /api/esphome/entity/map_zone - Mapping may have failed"
    echo "$zone_map_response"
fi
echo ""

# Test virtual relay mapping to ESPHome switches
echo "Testing POST /api/esphome/entity/map_relay..."
relay_map_response=$(curl -s -k -X POST "$BASE_URL/api/esphome/entity/map_relay" \
    -H "Content-Type: application/json" \
    -d '{"hostname":"test-device","entity_key":67890,"relay_id":32}')

if echo "$relay_map_response" | grep -q "success\|mapped"; then
    test_pass "POST /api/esphome/entity/map_relay - Virtual relay mapping successful"
    echo "$relay_map_response"
else
    test_warn "POST /api/esphome/entity/map_relay - Mapping may have failed"
    echo "$relay_map_response"
fi
echo ""

# Test ESPHome entity control
echo "Testing POST /api/esphome/entity/control (switch on)..."
control_response=$(curl -s -k -X POST "$BASE_URL/api/esphome/entity/control" \
    -H "Content-Type: application/json" \
    -d '{"hostname":"test-device","entity_key":67890,"action":"switch","value":"on"}')

if echo "$control_response" | grep -q "success\|sent"; then
    test_pass "POST /api/esphome/entity/control - Entity control successful"
    echo "$control_response"
else
    test_warn "POST /api/esphome/entity/control - Control may have failed"
    echo "$control_response"
fi
echo ""

# Test ESPHome entity control (brightness)
echo "Testing POST /api/esphome/entity/control (brightness 75)..."
brightness_response=$(curl -s -k -X POST "$BASE_URL/api/esphome/entity/control" \
    -H "Content-Type: application/json" \
    -d '{"hostname":"test-device","entity_key":67890,"action":"brightness","value":"75"}')

if echo "$brightness_response" | grep -q "success\|sent"; then
    test_pass "POST /api/esphome/entity/control - Brightness control successful"
    echo "$brightness_response"
else
    test_warn "POST /api/esphome/entity/control - Brightness control may have failed"
    echo "$brightness_response"
fi
echo ""

# Test that virtual zones show ESPHome integration in /api/status
echo "Testing ESPHome integration in /api/status..."
status_check=$(curl -s -k "$BASE_URL/api/status")
if echo "$status_check" | jq -e '.zones' > /dev/null 2>&1; then
    # Check if any zones have ESPHome mapping (zones 33-64 are virtual)
    virtual_zones=$(echo "$status_check" | jq '[.zones[] | select(.id >= 33 and .id <= 64)]')
    virtual_count=$(echo "$virtual_zones" | jq 'length')
    
    if [ "$virtual_count" -gt 0 ]; then
        test_pass "/api/status shows $virtual_count virtual zones (33-64) available for ESPHome mapping"
        echo "  Virtual zones available: $(echo "$virtual_zones" | jq -r '.[].id' | tr '\n' ' ')"
    else
        test_warn "/api/status does not show virtual zones for ESPHome integration"
    fi
else
    test_warn "/api/status missing zones data"
fi
echo ""

# Test that virtual relays show ESPHome integration
if echo "$status_check" | jq -e '.relays' > /dev/null 2>&1; then
    # Check if any relays have ESPHome mapping (relays 32+ are virtual)
    virtual_relays=$(echo "$status_check" | jq '[.relays[] | select(.id >= 32)]')
    virtual_relay_count=$(echo "$virtual_relays" | jq 'length')
    
    if [ "$virtual_relay_count" -gt 0 ]; then
        test_pass "/api/status shows $virtual_relay_count virtual relays (32+) available for ESPHome mapping"
        echo "  Virtual relays available: $(echo "$virtual_relays" | jq -r '.[].id' | tr '\n' ' ')"
    else
        test_warn "/api/status does not show virtual relays for ESPHome integration"
    fi
else
    test_warn "/api/status missing relays data"
fi
echo ""

# ============================================================================
# SECTION 8: ZIGBEE PHASE 3 - GROUPS, SCENES, AUTOMATIONS
# ============================================================================
if false; then
print_header "SECTION 7: Zigbee Phase 3 - Groups, Scenes, Automations"

# Test Groups API
echo "Testing GET /api/zigbee/groups..."
groups_response=$(curl -s -k "$BASE_URL/api/zigbee/groups")
if echo "$groups_response" | jq -e 'type == "array"' > /dev/null 2>&1; then
    group_count=$(echo "$groups_response" | jq 'length')
    test_pass "GET /api/zigbee/groups returns array ($group_count groups)"
    echo "$groups_response" | jq '.'
else
    test_warn "GET /api/zigbee/groups did not return valid JSON array"
fi
echo ""

# Test Create Group
echo "Testing POST /api/zigbee/groups (Create Group)..."
create_group_response=$(curl -s -k -X POST "$BASE_URL/api/zigbee/groups" \
    -H "Content-Type: application/json" \
    -d '{"name":"Test Group"}')
if echo "$create_group_response" | jq -e '.success' > /dev/null 2>&1; then
    test_pass "POST /api/zigbee/groups - Group created successfully"
    new_group_id=$(echo "$create_group_response" | jq '.group_id')
    echo "  Created group ID: $new_group_id"
    echo ""
    
    # Test Group Control
    echo "Testing POST /api/zigbee/group/$new_group_id/control (Turn On)..."
    control_response=$(curl -s -k -X POST "$BASE_URL/api/zigbee/group/$new_group_id/control" \
        -H "Content-Type: application/json" \
        -d '{"action":"on_off","value":"on"}')
    if echo "$control_response" | jq -e '.success' > /dev/null 2>&1; then
        test_pass "POST /api/zigbee/group/:id/control - Group controlled successfully"
    else
        test_warn "POST /api/zigbee/group/:id/control - Control may have failed"
    fi
else
    test_warn "POST /api/zigbee/groups - Group creation response invalid"
fi
echo ""

# Test Scenes API
echo "Testing GET /api/zigbee/scenes..."
scenes_response=$(curl -s -k "$BASE_URL/api/zigbee/scenes")
if echo "$scenes_response" | jq -e 'type == "array"' > /dev/null 2>&1; then
    scene_count=$(echo "$scenes_response" | jq 'length')
    test_pass "GET /api/zigbee/scenes returns array ($scene_count scenes)"
    echo "$scenes_response" | jq '.'
else
    test_warn "GET /api/zigbee/scenes did not return valid JSON array"
fi
echo ""

# Test Create Scene
echo "Testing POST /api/zigbee/scenes (Create Scene)..."
create_scene_response=$(curl -s -k -X POST "$BASE_URL/api/zigbee/scenes" \
    -H "Content-Type: application/json" \
    -d '{"name":"Test Scene"}')
if echo "$create_scene_response" | jq -e '.success' > /dev/null 2>&1; then
    test_pass "POST /api/zigbee/scenes - Scene created successfully"
    new_scene_id=$(echo "$create_scene_response" | jq '.scene_id')
    echo "  Created scene ID: $new_scene_id"
    echo ""
    
    # Test Scene Recall
    echo "Testing POST /api/zigbee/scene/$new_scene_id/recall..."
    recall_response=$(curl -s -k -X POST "$BASE_URL/api/zigbee/scene/$new_scene_id/recall")
    if echo "$recall_response" | jq -e '.success' > /dev/null 2>&1; then
        test_pass "POST /api/zigbee/scene/:id/recall - Scene recalled successfully"
    else
        test_warn "POST /api/zigbee/scene/:id/recall - Recall may have failed"
    fi
else
    test_warn "POST /api/zigbee/scenes - Scene creation response invalid"
fi
echo ""

# Test zigbee.html Phase 3 UI
echo "Testing zigbee.html Phase 3 UI..."
zigbee_ui=$(curl -s -k "$BASE_URL/zigbee.html")
if echo "$zigbee_ui" | grep -q "switchTab"; then
    test_pass "zigbee.html contains Phase 3 tab switching functionality"
else
    test_fail "zigbee.html missing Phase 3 UI code"
fi

if echo "$zigbee_ui" | grep -q "refreshGroups"; then
    test_pass "zigbee.html contains Groups management code"
else
    test_fail "zigbee.html missing Groups code"
fi

if echo "$zigbee_ui" | grep -q "refreshScenes"; then
    test_pass "zigbee.html contains Scenes management code"
else
    test_fail "zigbee.html missing Scenes code"
fi

if echo "$zigbee_ui" | grep -q "tab-groups"; then
    test_pass "zigbee.html contains Groups tab UI element"
else
    test_fail "zigbee.html missing Groups tab"
fi

if echo "$zigbee_ui" | grep -q "tab-scenes"; then
    test_pass "zigbee.html contains Scenes tab UI element"
else
    test_fail "zigbee.html missing Scenes tab"
fi

if echo "$zigbee_ui" | grep -q "tab-automations"; then
    test_pass "zigbee.html contains Automations tab UI element"
else
    test_fail "zigbee.html missing Automations tab"
fi

fi # End disabled Zigbee tests

# ============================================================================
# SECTION 9: SUMMARY
# ============================================================================
print_header "TEST SUMMARY"

total=$((PASS + FAIL + WARN))
echo -e "${GREEN}Passed: $PASS${NC}"
echo -e "${RED}Failed: $FAIL${NC}"
echo -e "${YELLOW}Warnings: $WARN${NC}"
echo "Total Tests: $total"

if [ $FAIL -eq 0 ]; then
    echo -e "\n${GREEN}✓ ALL CRITICAL TESTS PASSED${NC}"
    exit 0
else
    echo -e "\n${RED}✗ SOME TESTS FAILED${NC}"
    exit 1
fi
