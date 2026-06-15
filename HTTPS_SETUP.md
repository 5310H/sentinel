# HTTPS Implementation Guide

## Overview

Both the ESP32 main firmware and the Linux mock support HTTPS with TLS certificates loaded from persistent storage and an embedded fallback.

## ESP32 Implementation

### Certificate Management Strategy

The ESP32 uses a **3-tier certificate loading strategy**:

1. **Primary**: Load from NVS (fast, persistent)
2. **Fallback 1**: Load from SPIFFS if not in NVS
3. **Fallback 2**: Use embedded certificate (always available)

### Setup Process

1. **Generate Certificate**: Create `server.pem` using the provided script
2. **Copy to Data Directory**: Place `server.pem` in the `data/` folder (same as config.json, zones.json, etc.)
3. **Build & Flash**: The file is automatically included in SPIFFS just like other data files
4. **First Boot**: Certificate manager loads from SPIFFS → caches in NVS
5. **Subsequent Boots**: Loaded from NVS (fastest)

### Certificate File Location

- **Source**: `data/server.pem` - Where the certificate file is stored (like config.json, zones.json, etc.)
- **SPIFFS**: `/spiffs/server.pem` - Loaded at runtime from SPIFFS partition
- **NVS**: `certs/server_pem` - Cached copy for faster startup
- **Embedded**: Built into firmware (valid until 2036-01-25) - Used if file not found

### How It Works

1. **Initialization**: `cert_mgr_init()` is called during `storage_init()`
2. **Loading Priority**:
   - Check NVS first (fastest)
   - Check SPIFFS if not in NVS
   - Use embedded fallback if neither available
3. **TLS Usage**: When HTTPS connections arrive, `cert_mgr_get_cert()` returns the loaded certificate

### Code Structure (main.c)

```c
// Include certificate manager and embedded cert
#include "server_cert.h"  // Embedded HTTPS certificate (fallback)
#include "cert_mgr.h"     // Certificate manager for loading from storage

// In event handler:
if (ev == MG_EV_ACCEPT && fn_data != NULL) {  // fn_data != NULL for HTTPS
    struct mg_tls_opts opts = {
        .cert = cert_mgr_get_cert(),
        .key = cert_mgr_get_cert(),   // Same cert contains both cert and key
    };
    mg_tls_init(c, &opts);
}

// In mongoose_task:
mg_http_listen(&mgr, "http://0.0.0.0:80", fn, NULL);              // HTTP
mg_http_listen(&mgr, "https://0.0.0.0:443", fn, (void *)1);      // HTTPS
```

### Building

#### Standard Build (Recommended)

The certificate is treated like other data files (config.json, zones.json, etc.):

1. Ensure `server.pem` is in the `data/` directory:
```bash
cp server.pem data/server.pem
```

2. Build and flash normally:
```bash
idf.py build
idf.py flash
```

The certificate will be included in SPIFFS automatically, just like the JSON configuration files.

#### Upload via Web UI (After First Boot)

If certificate isn't available initially:
1. Device boots with embedded certificate
2. Upload new certificate through web UI admin panel
3. Certificate is saved to NVS

#### Using Embedded Certificate Only

Device automatically uses embedded certificate if:
- File not found in `data/` directory
- NVS entry missing
- No upload via web UI

This ensures the device **always** has working HTTPS.

### Ports
- HTTP: Port 80 (for compatibility)
- HTTPS: Port 443 (standard HTTPS port)

## Linux Mock Implementation

### Certificate Management
- **Location**: `./server.pem` - File in project root
- **Certificate**: Same self-signed certificate as ESP32 (valid until 2036-01-25)
- **Key**: RSA 2048-bit
- **Ports**:
  - HTTP: Port 8000 (for testing)
  - HTTPS: Port 8443 (for testing)

### How It Works

1. **File-based Loading**: The certificate is loaded from disk at startup
2. **TLS Initialization**: When HTTPS connections are accepted, the certificate file is loaded and used
3. **Dual Listeners**: Both HTTP and HTTPS listeners are active simultaneously

### Code Structure (test_linux/main_mock.c)

```c
// In web_handler:
if (ev == MG_EV_ACCEPT && c->fn_data != NULL) {  // fn_data != NULL for HTTPS
    struct mg_tls_opts opts = {
        .cert = mg_str("./server.pem"),
        .key = mg_str("./server.pem"),
    };
    mg_tls_init(c, &opts);
}

// In main():
mg_http_listen(&mgr, "http://0.0.0.0:8000", web_handler, NULL);          // HTTP
mg_http_listen(&mgr, "https://0.0.0.0:8443", web_handler, (void *)1);   // HTTPS
```

### Building and Running

```bash
cd test_linux
make
./sentinel_test
```

## Testing HTTPS

### Using curl

```bash
# HTTP request
curl http://localhost:8000/api/status

# HTTPS request (ignore self-signed certificate warning)
curl -k https://localhost:8443/api/status

# HTTPS with verbose output
curl -kv https://localhost:8443/api/status
```

### Browser Testing

1. **HTTP**: `http://localhost:8000` (Linux) or `http://<ESP32_IP>:80` (ESP32)
2. **HTTPS**: `https://localhost:8443` (Linux) or `https://<ESP32_IP>:443` (ESP32)
   - Browser will warn about self-signed certificate - this is normal
   - Click "Advanced" or "Proceed anyway" to continue

### Using openssl

```bash
# Test connection
openssl s_client -connect localhost:8443

# Verify certificate
openssl x509 -in server.pem -noout -text
openssl x509 -in server.pem -noout -dates
```

## Certificate Renewal

### When Needed
The current certificate is valid until 2036-01-25. When renewal is needed:

### Generate New Certificate

```bash
./scripts/generate_long_cert.sh
```

This creates a new `server.pem` file with a 10-year validity period.

### Update ESP32

#### Method 1: Update Data Directory (Next Build)

1. Copy new certificate to data directory (like config.json):
```bash
cp scripts/server.pem data/server.pem
```

2. Rebuild and flash:
```bash
idf.py clean
idf.py build
idf.py flash
```

#### Method 2: Update via Web UI Admin Panel

1. Access device web UI
2. Go to Settings/Admin
3. Upload new certificate file
4. Device saves to NVS and restarts with new cert

#### Method 3: Update SPIFFS Directly

If device is running and SPIFFS is mounted:
```bash
# Copy new certificate to SPIFFS
cp scripts/server.pem /spiffs/server.pem
# Restart device
```

### Update Linux Mock

1. Copy the new `server.pem` to the project root
2. Rebuild and run

```bash
cp scripts/server.pem ./server.pem
cd test_linux
make
./sentinel_test
```

## Security Considerations

### Self-Signed Certificate
⚠️ **Important**: These are self-signed certificates for testing/internal use only.

- **Pros**: No external CA needed, quick deployment
- **Cons**: Browser warnings, vulnerable to MITM without additional validation

### Production Deployment
For production systems:
1. Obtain certificates from a trusted CA (Let's Encrypt, etc.)
2. Implement certificate pinning in client applications
3. Use automatic renewal mechanisms (ACME/Let's Encrypt)

### Client Validation
When connecting programmatically, clients can:
- Disable validation for testing (curl `-k` flag, etc.)
- Pin the certificate fingerprint
- Validate against the embedded public key

## Troubleshooting

### HTTPS Not Working

1. **Check Listening Port**
   ```bash
   # Linux
   netstat -tulpn | grep 8443
   
   # ESP32 (via serial)
   Observe startup messages for port binding
   ```

2. **Certificate Path Issues**
   - **ESP32**: Ensure `server_cert.h` is included and has correct syntax
   - **Linux**: Ensure `server.pem` exists in current directory

3. **TLS Initialization Failure**
   - Check that `mg_tls_init()` is called with valid cert data
   - Verify certificate is in valid PEM format

### Certificate Validation Errors

```
# SSL certificate problem
curl: (60) SSL certificate problem: self signed certificate
```

Solution: Use `-k` flag to ignore certificate validation (for testing only)

```bash
curl -k https://localhost:8443
```

## Files Involved

### ESP32 Implementation
- `main/main.c` - Event handler with TLS support
- `main/server_cert.h` - Embedded certificate (auto-generated)

### Linux Mock
- `test_linux/main_mock.c` - Event handler with TLS support
- `server.pem` - Certificate file

### Certificate Management
- `scripts/generate_long_cert.sh` - Script to generate/renew certificates

## Additional Notes

- Both HTTP and HTTPS are available simultaneously for compatibility
- TLS initialization happens once per connection at the `MG_EV_ACCEPT` event
- The same certificate is used for both ESP32 and Linux mock for consistency
- Certificate renewal doesn't require code changes (except for ESP32 embedding step)

## References

- [Mongoose HTTPS/TLS Support](https://mongoose.ws/docs/tutorials/https.md)
- [mbedTLS Documentation](https://tls.mbed.org/)
- [Self-Signed Certificates](https://www.ssl.com/article/what-is-a-self-signed-certificate/)
