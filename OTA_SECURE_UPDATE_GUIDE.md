# Secure OTA Firmware Update Guide

## Overview

Sentinel-ESP includes a secure OTA (Over-The-Air) firmware update system with:
- ✅ **PIN Authentication** - Master PIN or user PIN required
- ✅ **Firmware Validation** - Magic number and header verification
- ✅ **HTTPS Encryption** - All uploads over TLS 1.2+
- ✅ **Partition Safety** - Validates available space before writing
- ✅ **Atomic Updates** - Rollback on failure with `esp_ota_mark_app_valid_cancel_rollback()`
- ✅ **OTA Partitions** - Dual partition scheme (ota_0, ota_1) for safe updates
- ✅ **Version Management** - Embedded version tracking with `/api/version` endpoint
- ✅ **Status Monitoring** - OTA progress tracking with `/api/ota/status` endpoint

## Security Features

### 1. Authentication

**All OTA uploads require valid PIN authentication:**

- Master PIN (from `config.pin`)
- Any authorized user PIN
- Transmitted via `Authorization: Bearer <PIN>` header

### 2. Firmware Validation

Before flashing, the system validates:

| Check | Description |
|-------|-------------|
| **Magic Number** | Must be `0xE9` (ESP32 image marker) |
| **Image Header** | Valid `esp_image_header_t` structure |
| **Size Limits** | Max 10MB firmware size |
| **Partition Space** | Sufficient OTA partition available |

### 3. Transport Security

- Endpoint: `https://sentinel.local/api/ota/upload`
- Protocol: HTTPS with TLS 1.2+
- Certificate: Self-signed or Let's Encrypt (see [CERTIFICATE_RENEWAL_ANALYSIS.md](CERTIFICATE_RENEWAL_ANALYSIS.md))

## Update Methods

### Method 1: Web Upload (Recommended)

**Step 1:** Build firmware
```bash
cd /home/kjgerhart/sentinel-esp
idf.py build
```

**Step 2:** Upload via curl
```bash
curl -X POST \
  -H "Authorization: Bearer YOUR_PIN_HERE" \
  -F "file=@build/sentinel-esp.bin" \
  https://sentinel.local/api/ota/upload
```

**Expected Response:**
```json
{
  "status": "success",
  "message": "Firmware updated, restarting in 3 seconds",
  "bytes": 873216
}
```

**Step 3:** Device restarts automatically after 3 seconds

### Method 2: Web UI Upload

1. Navigate to `https://sentinel.local/admin` (future feature)
2. Enter Master PIN or user PIN
3. Click "Upload Firmware"
4. Select `sentinel-esp.bin` file
5. Confirm update
6. Wait for restart (30-60 seconds)

### Method 3: Automated CI/CD

**GitHub Actions Example:**
```yaml
name: Deploy Firmware
on:
  push:
    tags:
      - 'v*'

jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Setup ESP-IDF
        uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: v5.2
      
      - name: Build Firmware
        run: idf.py build
      
      - name: Deploy to Production Device
        env:
          SENTINEL_PIN: ${{ secrets.MASTER_PIN }}
          SENTINEL_HOST: ${{ secrets.DEVICE_IP }}
        run: |
          curl -X POST \
            -H "Authorization: Bearer $SENTINEL_PIN" \
            -F "file=@build/sentinel-esp.bin" \
            https://$SENTINEL_HOST/api/ota/upload
```

## API Reference

### POST /api/ota/upload

**Authentication:** Required (Bearer token)

**Headers:**
```http
Authorization: Bearer <PIN>
Content-Type: multipart/form-data
```

**Body:**
```
file=@/path/to/firmware.bin
```

**Response Codes:**

| Code | Meaning |
|------|---------|
| 200 | Upload successful, device restarting |
| 400 | Invalid firmware format |
| 401 | Invalid or missing PIN |
| 500 | Flash error or OTA partition issue |

**Response Body (Success):**
```json
{
  "status": "success",
  "message": "Firmware updated, restarting in 3 seconds",
  "bytes": 873216
}
```

**Response Body (Error):**
```json
{
  "status": "error",
  "message": "Invalid firmware magic number"
}
```

### GET /api/version

**Authentication:** None required

**Response:**
```json
{
  "version": "2026.07.01",
  "running_partition": "ota_0",
  "boot_partition": "ota_0",
  "ota_capable": true
}
```

### GET /api/ota/status

**Authentication:** None required

**Response:**
```json
{
  "in_progress": false,
  "bytes_written": 0,
  "running_partition": "ota_0",
  "boot_partition": "ota_0",
  "ota_partition_available": true,
  "ota_partition": "ota_1",
  "ota_partition_size": 655360
}
```

## Update Process

### Lifecycle

```
1. Client uploads firmware → HTTP POST with PIN
2. Server authenticates PIN → Check against master/user PINs
3. Server validates firmware → Magic number + header checks
4. Server writes to OTA partition → esp_ota_begin/write/end
5. Server sets boot partition → esp_ota_set_boot_partition
6. Server responds "success" → Client receives confirmation
7. Server restarts (3s delay) → esp_restart()
8. Bootloader boots new firmware → From OTA partition
```

### Rollback Protection

If new firmware fails to boot, ESP32 bootloader automatically rolls back to previous working version after 3 failed boot attempts.

**Manual Rollback:**
```bash
# Flash previous working firmware via USB
cd /home/kjgerhart/sentinel-esp
idf.py flash
```

## Troubleshooting

### Error: "Unauthorized - valid PIN required"

**Cause:** Invalid or missing PIN

**Solution:**
```bash
# Verify PIN is correct
curl -X POST \
  -H "Authorization: Bearer 1234" \  # Replace with actual PIN
  -F "file=@build/sentinel-esp.bin" \
  https://sentinel.local/api/ota/upload
```

### Error: "Invalid firmware magic number"

**Cause:** Uploaded file is not a valid ESP32 firmware image

**Solution:**
1. Verify you're uploading `build/sentinel-esp.bin` (not `bootloader.bin` or `partition-table.bin`)
2. Rebuild firmware: `rm -rf build && idf.py build`
3. Check file size: `ls -lh build/sentinel-esp.bin` (should be 800KB-2MB)

### Error: "No OTA partition"

**Cause:** Partition table doesn't include OTA partitions

**Solution:**
1. Check partition table: `cat partitions.csv`
2. Ensure `ota_0` and `ota_1` partitions exist
3. Re-flash partition table: `idf.py partition-table-flash`

### Device doesn't restart after upload

**Cause:** Network timeout or upload incomplete

**Solution:**
1. Check upload response: Should be `"status":"success"`
2. Wait 60 seconds for automatic restart
3. Manual power cycle if needed
4. Check serial logs: `idf.py monitor`

### New firmware boots but doesn't work

**Cause:** Configuration incompatibility or missing dependencies

**Solution:**
1. Device will rollback after 3 failed boots automatically
2. Check logs via serial: `idf.py monitor`
3. Reflash working version via USB if needed

## Security Best Practices

### 1. Change Default PIN

```json
{
  "pin": "1234"  ← CHANGE THIS in config.json
}
```

### 2. Use HTTPS Certificate

- **Development:** Self-signed cert (current)
- **Production:** Let's Encrypt certificate (see [CERT_SCALE_SOLUTION.md](CERT_SCALE_SOLUTION.md))

### 3. Firmware Signing (Future Enhancement)

**Current:** Basic validation (magic number + header)  
**Planned:** RSA-2048 signature verification

To implement:
```c
// Generate signing key (one-time)
openssl genrsa -out ota_signing_key.pem 2048
openssl rsa -in ota_signing_key.pem -pubout -out ota_verify_key.pem

// Sign firmware before upload
openssl dgst -sha256 -sign ota_signing_key.pem -out firmware.sig build/sentinel-esp.bin

// Verify signature on device
mbedtls_rsa_pkcs1_verify(&rsa_ctx, NULL, NULL, MBEDTLS_RSA_PUBLIC,
    MBEDTLS_MD_SHA256, hash_len, hash, signature);
```

### 4. Limit Upload Rate

Add rate limiting to prevent brute-force PIN attacks:

```c
static uint32_t ota_attempt_count = 0;
static uint32_t ota_last_attempt = 0;

if (xTaskGetTickCount() - ota_last_attempt < pdMS_TO_TICKS(60000)) {
    ota_attempt_count++;
    if (ota_attempt_count > 3) {
        ESP_LOGW(TAG, "Too many OTA attempts, blocking for 5 minutes");
        mg_http_reply(c, 429, "Content-Type: application/json\r\n",
            "{\"status\":\"error\",\"message\":\"Too many attempts\"}");
        return;
    }
}
```

### 5. Audit Logging

Log all OTA attempts:

```c
ESP_LOGI(TAG, "OTA attempt from %s with PIN %s",
    inet_ntoa(c->rem.ip), authenticated ? "VALID" : "INVALID");

// Save to event log
system_monitoring_log_event(EVENT_OTA_UPDATE, 0);
```

## Firmware Build Process

### Standard Build

```bash
cd /home/kjgerhart/sentinel-esp
idf.py build
```

**Output:** `build/sentinel-esp.bin` (~849KB)

### Release Build (Optimized)

```bash
# Enable optimizations
idf.py menuconfig
# → Component config → Compiler options → Optimization Level → Release (-O2)

idf.py build
```

**Output:** Smaller binary (~750KB) with optimizations

### Debug Build (with Symbols)

```bash
idf.py menuconfig
# → Component config → Compiler options → Optimization Level → Debug (-Og)

idf.py build
```

**Output:** Larger binary (~1.2MB) with debug symbols

## Version Management

### Checking Current Version

```bash
# Via serial console
idf.py monitor

# Look for boot message:
# I (123) SENTINEL: Sentinel-ESP v2026.07.01 starting...
```

### Setting Version Number

**Edit:** `main/CMakeLists.txt`
```cmake
idf_component_register(
    SRCS "main.c" "hal_esp32.c" "rf_driver.c"
    INCLUDE_DIRS "."
    EMBED_FILES "version.txt"
)
```

**Create:** `main/version.txt`
```
2.4.1
```

**Read in code:**
```c
extern const char version_txt_start[] asm("_binary_version_txt_start");
extern const char version_txt_end[] asm("_binary_version_txt_end");

ESP_LOGI(TAG, "Firmware version: %.*s",
    (int)(version_txt_end - version_txt_start), version_txt_start);
```

## Production Deployment

### Pre-Update Checklist

- [ ] Test firmware on development device first
- [ ] Verify configuration compatibility
- [ ] Backup current firmware: `esptool.py read_flash 0x10000 0x200000 backup.bin`
- [ ] Ensure stable power supply (no battery backup during update)
- [ ] Schedule update during low-traffic period
- [ ] Notify users of planned maintenance

### Post-Update Verification

```bash
# 1. Check device reboots
ping sentinel.local

# 2. Verify web interface loads
curl -k https://sentinel.local/

# 3. Test authentication
curl -X POST https://sentinel.local/api/auth \
  -d '{"pin":"1234"}' \
  -H "Content-Type: application/json"

# 4. Check system status
curl -k https://sentinel.local/status

# 5. Monitor logs for errors
idf.py monitor
```

## Advanced Features (Future)

### 1. Differential Updates

Reduce bandwidth by sending only changed sectors:

```
Current: 849KB full firmware
Differential: ~100KB changed sectors only
```

### 2. Background Updates

Download firmware in background, apply on next reboot:

```c
// Download during idle time
xTaskCreate(ota_background_download, "ota_dl", 4096, NULL, 5, NULL);

// Apply on next scheduled restart
esp_ota_mark_app_valid_cancel_rollback();
```

### 3. Multi-Stage Rollout

Test on subset of devices before full deployment:

```
Stage 1: 10% of fleet (canary devices)
Wait 24h, monitor for issues
Stage 2: 50% of fleet
Wait 12h
Stage 3: Remaining 40%
```

## References

- [ESP-IDF OTA Updates](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/ota.html)
- [Mongoose Upload Handler](https://mongoose.ws/documentation/#mg_http_upload)
- [Certificate Management](CERTIFICATE_RENEWAL_ANALYSIS.md)
- [Security Hardening](SECURITY_HARDENING_IMPLEMENTATION.md)

---

**Last Updated:** 2026-01-29  
**Version:** 1.0  
**Status:** Production-ready with PIN authentication
