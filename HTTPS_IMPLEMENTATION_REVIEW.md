# HTTPS Implementation Review - ESP32

## Current Status: ⚠️ **INCOMPLETE**

The HTTPS implementation on ESP32 is **not finished**. The server currently only listens on HTTP port 80.

## Current Implementation

### What's Working
- ✅ HTTP server on port 80 (`mg_http_listen(&mgr, "http://0.0.0.0:80", fn, NULL)`)
- ✅ mbedTLS is enabled in ESP-IDF (sdkconfig shows `CONFIG_MBEDTLS_TLS_ENABLED=y`)
- ✅ Certificate file exists (`server.pem` contains both cert and key)
- ✅ Mongoose library supports HTTPS/TLS

### What's Missing
- ❌ **No HTTPS listener** - Only HTTP port 80 is configured
- ❌ **No TLS initialization** - `mg_tls_init()` is never called
- ❌ **No certificate loading** - Certificate file is not loaded into mongoose
- ❌ **No HTTPS port** - Port 443 (or 8443) is not configured

## Current Code (main/main.c)

```c
void mongoose_task(void *pvParameters) {
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, "http://0.0.0.0:80", fn, NULL);  // HTTP only
    // ❌ Missing: HTTPS listener
    for (;;) {
        mg_mgr_poll(&mgr, 50);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
```

## Required Changes

### 1. Add HTTPS Listener

The server needs to listen on both HTTP and HTTPS ports:

```c
// HTTP on port 80 (for compatibility)
mg_http_listen(&mgr, "http://0.0.0.0:80", fn, NULL);

// HTTPS on port 443 (or 8443 for testing)
mg_http_listen(&mgr, "https://0.0.0.0:443", fn, (void *)1);
```

### 2. Initialize TLS on HTTPS Connections

The event handler needs to initialize TLS when HTTPS connections are accepted:

```c
static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    if (ev == MG_EV_ACCEPT && fn_data != NULL) {
        // fn_data is non-NULL for HTTPS connections
        struct mg_tls_opts opts = {
            .cert = mg_str("/spiffs/server.pem"),  // Certificate file path
            .key = mg_str("/spiffs/server.pem"),   // Key file (same file contains both)
            // .ca = mg_str(""),  // Optional: CA cert for client auth
        };
        mg_tls_init(c, &opts);
    } else if (ev == MG_EV_HTTP_MSG) {
        // ... existing HTTP handling ...
    }
}
```

### 3. Certificate File Location

The `server.pem` file needs to be:
- Stored in SPIFFS at `/spiffs/server.pem`
- Or embedded in the firmware
- Or loaded from NVS/other storage

## Implementation Steps

### Step 1: Copy Certificate to SPIFFS

The `server.pem` file needs to be accessible from SPIFFS. Options:
1. **Embed in firmware** (simpler, but increases binary size)
2. **Store in SPIFFS** (requires SPIFFS partition and file upload)
3. **Generate at runtime** (complex, not recommended)

### Step 2: Update Event Handler

Modify the `fn()` function in `main/main.c` to handle TLS initialization.

### Step 3: Add HTTPS Listener

Add the HTTPS listener alongside the HTTP listener.

### Step 4: Test

Test with:
```bash
curl -k https://<esp32-ip>:443
```

## Security Considerations

### Current Issues
- ⚠️ **Self-signed certificate** - `server.pem` appears to be self-signed
- ⚠️ **No certificate validation** - Clients will get security warnings
- ⚠️ **Mixed HTTP/HTTPS** - HTTP is still available (security risk)

### Recommendations
1. **Use proper certificate** - Get a certificate from a CA or use Let's Encrypt
2. **Disable HTTP** - Remove HTTP listener in production (or redirect to HTTPS)
3. **Certificate rotation** - Implement certificate update mechanism
4. **HSTS headers** - Add HTTP Strict Transport Security headers

## Example Implementation

See `managed_components/cesanta__mongoose/mongoose/examples/http-restful-server/main.c` for a complete HTTPS example.

## Files That Need Changes

1. **`main/main.c`**:
   - Add HTTPS listener
   - Update event handler to initialize TLS
   - Load certificate from SPIFFS

2. **`CMakeLists.txt` or partition table**:
   - Ensure SPIFFS partition is large enough for certificate
   - Or embed certificate in firmware

3. **Build system**:
   - Copy `server.pem` to SPIFFS during build (if using SPIFFS)

## Testing

### Test HTTP (should work)
```bash
curl http://<esp32-ip>:80/api/status
```

### Test HTTPS (currently fails - not implemented)
```bash
curl -k https://<esp32-ip>:443/api/status
```

## Priority

**Medium-High** - HTTPS is important for:
- Secure authentication (2FA tokens, passwords)
- Preventing man-in-the-middle attacks
- Production deployment

However, for local network use, HTTP may be acceptable.

---

**Conclusion**: HTTPS implementation is **incomplete** and needs to be finished before production deployment.
