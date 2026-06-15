# Camera Quick Start Guide

## Adding Your First Thingino Camera

### Step 1: Flash Thingino to Your Camera
Visit: https://github.com/themactep/thingino-firmware

Supported cameras:
- Wyze Cam v3 (recommended)
- TP-Link Tapo C200
- Sonoff GK-200MP2-B
- Xiaomi Xiaofang 1S

### Step 2: Configure Camera Network
1. Connect camera to your network
2. Set static IP or note mDNS hostname
3. Verify HTTP snapshot works:
   ```bash
   curl http://192.168.1.100/image.jpg --output test.jpg
   ```

### Step 3: Add to Sentinel
1. Navigate to: `https://<sentinel-ip>/cameras.html`
2. Click "Add Camera"
3. Fill in:
   - **Friendly Name:** Front Door Camera
   - **Hostname:** 192.168.1.100
   - **Port:** 80
   - **ESPHome ID:** camera (keep default)
   - **Enabled:** ✓ checked
4. Click "Save Camera"
5. Click "Test" to verify connectivity

### Step 4: Verify Alarm Snapshots
1. Trigger a test alarm (breach a zone)
2. Check SD card for snapshots:
   ```bash
   ls /sd/alarms/
   # Should see: 20260131_143022_zone01_cam0.jpg
   ```

## Supported URL Formats

Sentinel automatically detects your camera type:

| Camera Type | URL Pattern |
|------------|-------------|
| **Thingino** | `http://ip/image.jpg` |
| **ESPHome** | `http://ip/camera/snapshot` |
| **Generic** | `http://ip/snapshot.jpg` |
| **CGI** | `http://ip/cgi-bin/snapshot.cgi` |

## API Quick Reference

```bash
# List all cameras
curl https://<sentinel-ip>/api/cameras

# Get snapshot
curl https://<sentinel-ip>/api/camera/0/snapshot --output snapshot.jpg

# Add camera
curl -X POST https://<sentinel-ip>/api/camera/add \
  -H "Content-Type: application/json" \
  -d '{"hostname":"192.168.1.100","port":80,"friendly_name":"Front Door","esphome_id":"camera","enabled":true}'

# Test camera
curl -X POST https://<sentinel-ip>/api/camera/0/test

# Toggle camera
curl -X POST https://<sentinel-ip>/api/camera/0/toggle

# Delete camera
curl -X DELETE https://<sentinel-ip>/api/camera/0
```

## Troubleshooting

**Camera shows offline:**
```bash
# 1. Test network connectivity
ping 192.168.1.100

# 2. Test HTTP endpoint
curl -I http://192.168.1.100/image.jpg

# 3. Check Sentinel logs
idf.py monitor | grep -i camera
```

**Snapshots not saving:**
```bash
# 1. Verify SD card mounted
ls /sd/alarms/

# 2. Check free space
df -h /sd

# 3. Check permissions
ls -la /sd/alarms/
```

## Network Isolation (Security)

**IMPORTANT:** Cameras should be on isolated VLAN

```
Router Configuration:
├─ VLAN 1 (Main Network) - 192.168.1.0/24
│  └─ Sentinel ESP32: 192.168.1.50
├─ VLAN 10 (Camera Network) - 192.168.10.0/24
│  ├─ Front Door Camera: 192.168.10.100
│  ├─ Garage Camera: 192.168.10.101
│  └─ Backyard Camera: 192.168.10.102
└─ Firewall Rules:
   ✓ VLAN 10 → VLAN 1 (port 80/443) - Allow
   ✗ VLAN 10 → Internet - Block
   ✗ VLAN 1 → VLAN 10 - Block (except Sentinel)
```

## Performance Tips

- **Snapshot Quality:** 10-15 (lower = better quality, larger file)
- **Max Cameras:** 8 devices (configurable in camera_mgr.h)
- **Fetch Timeout:** 5 seconds (adjust in camera_mgr.c if needed)
- **Memory Usage:** ~100KB per camera buffer
- **SD Card:** Class 10 or better recommended

## Files Modified

```
main/
  ├─ CMakeLists.txt          # Added camera component
  └─ main.c                  # Added API handlers + init

components/
  ├─ camera/
  │  └─ camera_mgr.c         # Multi-URL support
  └─ engine/
     └─ engine.c             # Alarm snapshot triggers

data/
  ├─ cameras.html            # Web UI (existing)
  └─ cameras.json            # Configuration (existing)
```

## Next Steps

1. **Build firmware:** `idf.py build`
2. **Flash device:** `idf.py flash monitor`
3. **Add cameras:** Visit web UI
4. **Test alarms:** Verify snapshots saved

---

**Status:** ✅ Implementation Complete  
**Compatibility:** ESPHome + Thingino  
**Security:** Audited and approved ([THINGINO_SECURITY_AUDIT.md](THINGINO_SECURITY_AUDIT.md))
