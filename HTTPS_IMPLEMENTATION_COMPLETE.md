# HTTPS Implementation Summary

## What Was Implemented

### 1. **ESP32 Certificate Manager** (`components/storage/cert_mgr.c/h`)
   - Smart 3-tier certificate loading:
     - **Tier 1**: Load from NVS (fast, persistent) ← **Primary**
     - **Tier 2**: Load from SPIFFS ← **Fallback 1** 
     - **Tier 3**: Use embedded certificate ← **Fallback 2** (always works)
   - Automatic NVS caching for faster startup
   - Update API for new certificates
   - Diagnostic functions for cert info

### 2. **ESP32 Main Firmware Updates** (`main/main.c`)
   - Integrated certificate manager
   - TLS initialization on HTTPS accept event
   - Dual HTTP/HTTPS listeners:
     - HTTP: Port 80 (compatibility)
     - HTTPS: Port 443 (secure)

### 3. **Linux Mock HTTPS Support** (`test_linux/main_mock.c`)
   - TLS initialization in web_handler
   - File-based certificate loading (`./server.pem`)
   - Dual HTTP/HTTPS listeners:
     - HTTP: Port 8000 (testing)
     - HTTPS: Port 8443 (testing)

### 4. **Build Infrastructure**
   - `scripts/prepare_spiffs.sh` - Prepare SPIFFS partition with certificate
   - `scripts/generate_long_cert.sh` - Generate 10-year self-signed certificate
   - `data/server.pem` - Certificate file (treated like config.json, zones.json, etc.)
   - `main/server_cert.h` - Embedded fallback certificate

### 5. **Documentation**
   - **HTTPS_QUICK_START.md** - Quick setup guide with step-by-step instructions
   - **HTTPS_SETUP.md** - Comprehensive HTTPS documentation
   - **HTTPS_IMPLEMENTATION_REVIEW.md** - Technical review (original)
   - **HTTPS_RECOMMENDATION.md** - Original recommendations (original)

## How It Works

### ESP32 Startup Sequence

```
1. app_main() starts
   ├─ NVS Flash initialized
   ├─ SPIFFS mounted
   └─ storage_init() called
      └─ cert_mgr_init() runs
         ├─ Check NVS for certificate
         ├─ If not found, check SPIFFS
         ├─ If not found, use embedded cert
         └─ Certificate ready for HTTPS

2. mongoose_task() starts
   ├─ HTTP listener on :80
   └─ HTTPS listener on :443 (with fn_data = (void *)1)

3. When HTTPS connection arrives
   ├─ MG_EV_ACCEPT event fires
   ├─ fn_data != NULL, so TLS init is triggered
   ├─ cert_mgr_get_cert() returns loaded certificate
   ├─ mg_tls_init() initializes TLS with certificate
   └─ HTTPS handshake completes
```

### Linux Mock Startup Sequence

```
1. main() initializes
   ├─ Loads all storage from disk
   ├─ Initializes engine/monitor
   └─ mg_mgr_init() creates manager

2. HTTP listener on :8000
   └─ HTTPS listener on :8443 (with fn_data = (void *)1)

3. When HTTPS connection arrives
   ├─ MG_EV_ACCEPT event fires
   ├─ fn_data != NULL, so TLS init is triggered
   ├─ Loads ./server.pem from current directory
   ├─ mg_tls_init() initializes TLS with certificate
   └─ HTTPS handshake completes
```

## Certificate Storage Strategy

### Advantages of This Approach

✅ **Flexible**: Certificate can be updated without rebuilding firmware
✅ **Efficient**: NVS caching avoids SPIFFS reads on every boot
✅ **Resilient**: Always falls back to embedded cert if storage fails
✅ **Updateable**: Web UI can upload new certificates
✅ **Standard**: Uses industry-standard PEM format

### Loading Priority

1. **NVS** (fastest - cached copy) → Use immediately
2. **SPIFFS** (slower - filesystem read) → Copy to NVS for next boot
3. **Embedded** (always available) → Fallback if above missing

## Usage Instructions

### Quick Start (ESP32)

```bash
# 1. Generate certificate (one time)
./scripts/generate_long_cert.sh

# 2. Copy to data directory (like config.json, zones.json, etc.)
cp server.pem data/server.pem

# 3. Build and flash
idf.py build
idf.py flash

# 4. Test
curl -k https://<ESP32_IP>:443/api/status
```

### Quick Start (Linux Mock)

```bash
# Build and run
cd test_linux
make
./sentinel_test

# Test
curl http://localhost:8000/api/status
curl -k https://localhost:8443/api/status
```

## Testing

### HTTP (should work)
```bash
# ESP32
curl http://<ESP32_IP>:80/api/status

# Linux Mock
curl http://localhost:8000/api/status
```

### HTTPS (should work with -k flag)
```bash
# ESP32
curl -kv https://<ESP32_IP>:443/api/status

# Linux Mock  
curl -kv https://localhost:8443/api/status
```

### Browser Testing
- HTTP: `http://<IP>:80` or `http://localhost:8000`
- HTTPS: `https://<IP>:443` or `https://localhost:8443`
  - Browser will warn about self-signed cert (normal)
  - Click "Advanced" → "Proceed anyway"

## Certificate Details

- **Type**: Self-signed RSA 2048-bit
- **Format**: PEM (combined cert + key)
- **Valid From**: 2026-01-27
- **Valid Until**: 2036-01-25 (10 years)
- **Subject**: CN=Sentinel, O=Sentinel Security System, C=US

## Files Changed/Created

### Created Files
- `components/storage/cert_mgr.h` - Certificate manager header
- `components/storage/cert_mgr.c` - Certificate manager implementation
- `main/server_cert.h` - Embedded certificate (fallback)
- `HTTPS_QUICK_START.md` - Quick start guide
- `HTTPS_SETUP.md` - Detailed setup documentation

### Modified Files
- `main/main.c` - Added cert_mgr support and TLS initialization
- `test_linux/main_mock.c` - Added TLS initialization for HTTPS
- `components/storage/storage_mgr.c` - Initialize cert_mgr at startup
- `data/server.pem` - Certificate file (source like config.json, zones.json)

## Key Components

### Certificate Manager (`cert_mgr.c`)
Handles all certificate loading and storage logic:
- NVS read/write
- SPIFFS file loading
- Embedded fallback
- Automatic caching

### Main Event Handler (`main.c`)
Responds to HTTPS connection events:
```c
if (ev == MG_EV_ACCEPT && fn_data != NULL) {
    struct mg_tls_opts opts = {
        .cert = cert_mgr_get_cert(),
        .key = cert_mgr_get_cert(),
    };
    mg_tls_init(c, &opts);
}
```

### Mongoose Listeners
Sets up dual HTTP/HTTPS:
```c
mg_http_listen(&mgr, "http://0.0.0.0:80", fn, NULL);        // HTTP
mg_http_listen(&mgr, "https://0.0.0.0:443", fn, (void *)1); // HTTPS
```

## Fallback Behavior

If `server.pem` is not found during setup:
1. Device boots with embedded certificate
2. HTTPS still works (fallback cert used)
3. You can upload new certificate via web UI
4. Or place `server.pem` in SPIFFS and restart

This ensures the device **always** has working HTTPS.

## Next Steps

1. **Generate certificate** (one time):
   ```bash
   ./scripts/generate_long_cert.sh
   ```

2. **Copy to data directory** (like other config files):
   ```bash
   cp server.pem data/server.pem
   ```

3. **Build and flash**:
   ```bash
   idf.py build
   idf.py flash
   ```

4. **Test HTTPS**:
   ```bash
   curl -k https://<ESP32_IP>:443/api/status
   ```

5. **Verify logs** show certificate was loaded successfully

## Support

For detailed information, see:
- `HTTPS_QUICK_START.md` - Step-by-step setup guide
- `HTTPS_SETUP.md` - Complete documentation with all options
- `components/storage/cert_mgr.c` - Implementation details
