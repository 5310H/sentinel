# HTTPS Certificate Setup Guide

## Quick Start

### 1. Generate Certificate (if not already created)

```bash
cd /home/kjgerhart/sentinel-esp
./scripts/generate_long_cert.sh
```

This creates `server.pem` in the project root, valid for 10 years (until 2036-01-25).

### 2. Copy to Data Directory

The certificate is automatically loaded from the `data/` directory like the JSON configuration files:

```bash
# The script above already created server.pem in the root
# Copy it to the data directory (same as config.json, zones.json, etc.)
cp server.pem data/server.pem
```

### 3. Build and Flash ESP32

```bash
idf.py build
idf.py flash
```

The certificate will be loaded from SPIFFS on first boot and cached in NVS for faster startup.

### 4. Verify Setup

Once the device boots:

1. **Check logs**: Look for "Loaded certificate from SPIFFS" message
2. **Test HTTP**: `curl http://<ESP32_IP>:80/api/status`
3. **Test HTTPS**: `curl -k https://<ESP32_IP>:443/api/status`

## Linux Mock Testing

For the Linux mock development/testing environment:

```bash
# Ensure server.pem is in project root
ls -la server.pem

# Build and run
cd test_linux
make clean
make
./sentinel_test

# Test in another terminal:
curl http://localhost:8000/api/status
curl -k https://localhost:8443/api/status
```

## Certificate Loading Flow

```
On ESP32 First Boot:
├─ SPIFFS mounts ✓
├─ cert_mgr_init() called
│  ├─ Try load from NVS → Not found
│  ├─ Try load from SPIFFS → server.pem found ✓
│  ├─ Save to NVS for caching ✓
│  └─ Ready for HTTPS
└─ mongoose_task starts with HTTPS enabled

On Subsequent Boots:
├─ SPIFFS mounts
├─ cert_mgr_init() called
│  ├─ Try load from NVS → Found! ✓ (fast path)
│  └─ Ready for HTTPS
└─ mongoose_task starts with HTTPS enabled

If Certificate Missing:
├─ SPIFFS: /spiffs/server.pem not found
├─ NVS: Entry missing
├─ Fallback to embedded certificate ✓ (always works)
└─ HTTPS still available with fallback cert
```

## Troubleshooting

### Certificate Not Loading

1. **Check SPIFFS mount**:
   ```
   UART log should show: "SPIFFS Mounted: X/Y KB used"
   ```

2. **Verify certificate in SPIFFS**:
   ```bash
   idf.py monitor  # Watch for cert_mgr messages
   ```

3. **Check file location**:
   ```bash
   # During build:
   ls -la build/spiffs_image/server.pem
   ```

### HTTPS Connection Refused

1. **Verify port 443 is open**:
   ```bash
   curl -kv https://<ESP32_IP>:443
   ```

2. **Check mongoose is running**:
   - Look for "mongoose" task in task list
   - Should see "https://0.0.0.0:443" listener log

3. **Try HTTP first**:
   ```bash
   curl http://<ESP32_IP>:80/api/status
   ```

### Certificate Validation Errors

This is **normal** for self-signed certificates:

```
curl: (60) SSL certificate problem: self signed certificate
```

Solution: Use curl with `-k` flag to ignore validation (testing only):

```bash
curl -k https://localhost:443/api/status
```

## Certificate Renewal

When the current certificate expires (2036-01-25):

1. Generate new certificate:
   ```bash
   ./scripts/generate_long_cert.sh
   ```

2. Prepare SPIFFS:
   ```bash
   ./scripts/prepare_spiffs.sh
   ```

3. Rebuild and flash:
   ```bash
   idf.py build
   idf.py flash
   ```

OR update via web UI admin panel if device is running.

## Files Involved

| File | Purpose |
|------|---------|
| `scripts/generate_long_cert.sh` | Generate self-signed certificate |
| `scripts/prepare_spiffs.sh` | Copy cert to SPIFFS build directory |
| `server.pem` | Self-signed certificate (created by generate script) |
| `main/server_cert.h` | Embedded certificate (fallback) |
| `components/storage/cert_mgr.h` | Certificate manager header |
| `components/storage/cert_mgr.c` | Certificate manager implementation |
| `test_linux/main_mock.c` | Linux mock HTTPS support |

## Additional Resources

- [HTTPS_SETUP.md](../HTTPS_SETUP.md) - Detailed HTTPS documentation
- [scripts/generate_long_cert.sh](generate_long_cert.sh) - Certificate generation
- [components/storage/cert_mgr.c](../components/storage/cert_mgr.c) - Certificate loading logic
