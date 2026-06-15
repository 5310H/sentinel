# HTTPS Implementation Checklist

## Pre-Build Checklist

### ESP32 Main Firmware

- [ ] Verify `server.pem` exists in data directory (same as config.json, zones.json)
  ```bash
  ls -la data/server.pem
  ```
  If missing, generate and copy it:
  ```bash
  ./scripts/generate_long_cert.sh
  cp server.pem data/server.pem
  ```

- [ ] Verify certificate manager components exist
  ```bash
  ls -la components/storage/cert_mgr.{h,c}
  ```

- [ ] Check that `main/main.c` includes cert_mgr
  ```bash
  grep -n "cert_mgr.h" main/main.c
  ```

- [ ] Check that `components/storage/storage_mgr.c` initializes cert_mgr
  ```bash
  grep -n "cert_mgr_init" components/storage/storage_mgr.c
  ```

### Linux Mock

- [ ] Verify `server.pem` exists in project root
  ```bash
  ls -la server.pem
  ```

- [ ] Check that `test_linux/main_mock.c` has TLS initialization
  ```bash
  grep -n "mg_tls_init" test_linux/main_mock.c
  ```

- [ ] Check that HTTPS listener is configured
  ```bash
  grep -n "https://0.0.0.0:8443" test_linux/main_mock.c
  ```

## Build Steps

### ESP32

1. **Verify Certificate in Data Directory**
   ```bash
   ls -la data/server.pem
   ```
   If missing:
   ```bash
   cp server.pem data/server.pem
   ```

2. **Clean Build** (first time or after major changes)
   ```bash
   idf.py clean
   ```

3. **Build**
   ```bash
   idf.py build
   ```
   Expected output should NOT have errors related to cert_mgr

4. **Flash**
   ```bash
   idf.py flash
   ```

5. **Monitor** (in separate terminal)
   ```bash
   idf.py monitor
   ```

### Linux Mock

1. **Clean**
   ```bash
   cd test_linux
   make clean
   ```

2. **Build**
   ```bash
   make
   ```

3. **Verify executable**
   ```bash
   ls -la sentinel_test
   ```

## Post-Build Verification

### ESP32 Serial Output (Monitor)

Look for these messages:

```
STORAGE_MGR: SPIFFS Mounted: X/Y KB used
STORAGE_MGR: Initializing certificate manager...
CERT_MGR: Initializing certificate manager
CERT_MGR: Certificate not in NVS, checking SPIFFS...
CERT_MGR: Loaded certificate from SPIFFS (3415 bytes)
CERT_MGR: Certificate cached in NVS for faster startup
```

Or if using embedded:

```
CERT_MGR: Certificate not in NVS, checking SPIFFS...
CERT_MGR: Certificate not found in storage, using embedded fallback
CERT_MGR: Using embedded fallback certificate (3415 bytes)
```

### Run ESP32

1. **Connect to network** (device should obtain IP)
2. **Check OLED** shows "Sentinel Online" with IP address
3. **Get IP address** from UART output or OLED display

### Test ESP32 HTTPS

In another terminal:

```bash
# Replace <ESP32_IP> with actual device IP
curl -kv https://<ESP32_IP>:443/api/status

# Should return JSON response
# May warn about self-signed cert (normal with -k flag)
```

### Test Linux Mock

1. **Run server** (in one terminal)
   ```bash
   cd test_linux
   ./sentinel_test
   ```
   
   Expected output:
   ```
   === SENTINEL FULL MOCK SERVER ===
   >> Web UI (HTTP):  http://localhost:8000
   >> Web UI (HTTPS): https://localhost:8443 (self-signed cert)
   ```

2. **Test in another terminal**
   ```bash
   # HTTP test
   curl http://localhost:8000/api/status
   
   # HTTPS test
   curl -k https://localhost:8443/api/status
   
   # Both should return similar JSON responses
   ```

## Testing Endpoints

### Common API Endpoints

```bash
# Get system status
curl -k https://<IP>:443/api/status

# Get zones
curl -k https://<IP>:443/api/zones

# Get users
curl -k https://<IP>:443/api/users

# Get logs
curl -k https://<IP>:443/api/logs

# Test with verbose output to see TLS handshake
curl -kv https://<IP>:443/api/status
```

### Browser Testing

1. Open browser
2. Navigate to `https://<ESP32_IP>:443`
3. Click through certificate warning (normal for self-signed)
4. Should see web UI normally

## Troubleshooting

### Build Errors

**Error: `cert_mgr.h: No such file or directory`**
- Verify file exists: `ls -la components/storage/cert_mgr.h`
- Check CMakeLists.txt includes path

**Error: `undefined reference to cert_mgr_init`**
- Ensure `cert_mgr.c` is compiled
- Check component CMakeLists.txt includes cert_mgr.c in SRCS

### Runtime Errors

**HTTPS connections refused**
- Check device obtained IP (look at OLED or UART output)
- Verify mongoose_task started
- Look for error messages in monitor output

**Self-signed certificate error**
- This is NORMAL for self-signed certs
- Use `curl -k` flag to ignore validation (testing)
- Browser users: click "Advanced" → "Proceed anyway"

**Certificate not found in storage**
- Check `prepare_spiffs.sh` ran successfully
- Check `/spiffs/server.pem` was created
- Fallback to embedded cert should activate automatically

## Success Criteria

✅ Build completes without errors
✅ Device boots without errors
✅ Device obtains IP address
✅ HTTP requests work: `curl http://<IP>:80/api/status`
✅ HTTPS requests work: `curl -k https://<IP>:443/api/status`
✅ Logs show certificate was loaded or fallback used
✅ Browser can access HTTPS with warning (expected)

## Next Steps After Verification

1. ✅ Verify HTTPS works on both HTTP and HTTPS ports
2. ✅ Test from browser and curl
3. ✅ Update any client code to use HTTPS URLs
4. ✅ Configure certificate pinning if needed (optional)
5. ✅ Plan for certificate renewal (valid until 2036-01-25)

## Emergency Fallback

If something goes wrong with SPIFFS or certificate:

1. **Device still boots** (embedded cert always works)
2. **HTTPS still available** (uses embedded fallback)
3. **No data loss** (fallback doesn't prevent operation)
4. **Can fix later** by uploading new cert via web UI

This design ensures HTTPS is **always available**.

## Support Documents

- **HTTPS_QUICK_START.md** - Step-by-step setup
- **HTTPS_SETUP.md** - Complete documentation
- **SPIFFS_SETUP.md** - SPIFFS partition configuration
- **HTTPS_IMPLEMENTATION_COMPLETE.md** - Technical overview
- **components/storage/cert_mgr.c** - Implementation details
