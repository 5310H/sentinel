# Certificate Renewal Strategy for ESP32

## The Problem

ESP32 devices are embedded systems that should ideally run for years without updates. Certificate expiration creates a maintenance burden:
- Certificates typically expire in 1-3 years
- Updating certificates requires firmware updates or manual intervention
- Expired certificates break HTTPS connections
- Users may not notice until it's too late

## Solution Options

### Option 1: Long-Lived Self-Signed Certificate ⭐ **RECOMMENDED**

**Generate a certificate with 10+ year expiration:**

```bash
# Generate certificate valid for 10 years
openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt \
  -days 3650 -nodes -subj "/CN=Sentinel-ESP/O=Home Security"

# Combine into PEM format (what mongoose expects)
cat server.crt server.key > server.pem
```

**Pros:**
- ✅ No renewal needed for 10+ years
- ✅ Works immediately, no CA needed
- ✅ Perfect for local network use
- ✅ No ongoing maintenance

**Cons:**
- ⚠️ Browser security warnings (users must accept)
- ⚠️ Not suitable for public internet (but you're using local network)

**Best For:** Local network security systems (your use case)

---

### Option 2: Certificate Update via Web Interface

**Allow uploading new certificates through the web UI:**

```c
// Add endpoint: POST /api/cert/update
// Accepts PEM file upload
// Stores in SPIFFS
// Restarts HTTPS listener with new cert
```

**Pros:**
- ✅ Can update without firmware flash
- ✅ Flexible - can use any certificate
- ✅ Can switch to CA-signed cert later

**Cons:**
- ⚠️ Requires implementation
- ⚠️ Still need to remember to update
- ⚠️ Security risk if not properly authenticated

**Best For:** Systems that need flexibility

---

### Option 3: Skip HTTPS for Local Network

**Use HTTP only on local network:**

```c
// Only HTTP listener
mg_http_listen(&mgr, "http://0.0.0.0:80", fn, NULL);
// No HTTPS
```

**Pros:**
- ✅ No certificate management
- ✅ No expiration issues
- ✅ Simpler implementation
- ✅ Works fine on trusted local network

**Cons:**
- ⚠️ Traffic not encrypted
- ⚠️ Vulnerable to local network attacks
- ⚠️ 2FA tokens sent in plain text

**Best For:** Completely isolated networks

---

### Option 4: Reverse Proxy with HTTPS

**Use a gateway/router for HTTPS termination:**

```
Internet → Router (HTTPS) → ESP32 (HTTP)
```

**Pros:**
- ✅ ESP32 doesn't need certificates
- ✅ Router handles certificate renewal
- ✅ Can use Let's Encrypt on router

**Cons:**
- ⚠️ Requires additional hardware/software
- ⚠️ More complex setup
- ⚠️ Router becomes single point of failure

**Best For:** Systems with existing network infrastructure

---

### Option 5: Embedded Certificate with Auto-Renewal

**Generate certificate at first boot, store in NVS:**

```c
// On first boot:
// 1. Generate self-signed cert (10 year expiration)
// 2. Store in NVS
// 3. Use for HTTPS

// Certificate auto-generates if missing
```

**Pros:**
- ✅ No manual certificate management
- ✅ Each device has unique certificate
- ✅ Long expiration built-in

**Cons:**
- ⚠️ Complex implementation
- ⚠️ Still expires eventually
- ⚠️ Requires crypto library for generation

**Best For:** Mass deployment scenarios

---

## Recommended Approach: Hybrid Solution

### Primary: Long-Lived Self-Signed Certificate

1. **Generate 10-year certificate** (see script below)
2. **Embed in firmware** or store in SPIFFS
3. **Document expiration date** (set reminder for year 9)
4. **Use for local network** (acceptable security warnings)

### Secondary: Certificate Update Mechanism

1. **Add web UI endpoint** to upload new certificate
2. **Store in SPIFFS** (can update without firmware flash)
3. **Restart HTTPS listener** with new certificate
4. **Admin-only access** (requires authentication)

### Implementation Priority

1. **Phase 1**: Use long-lived self-signed cert (10 years)
2. **Phase 2**: Add certificate upload endpoint (optional, for flexibility)

---

## Certificate Generation Script

Create `scripts/generate_long_cert.sh`:

```bash
#!/bin/bash
# Generate 10-year self-signed certificate for ESP32

DAYS=3650  # 10 years
KEY_SIZE=2048
CN="Sentinel-ESP"
OUTPUT="server.pem"

echo "Generating certificate valid for $DAYS days..."

# Generate private key and certificate
openssl req -x509 -newkey rsa:$KEY_SIZE \
  -keyout server.key \
  -out server.crt \
  -days $DAYS \
  -nodes \
  -subj "/CN=$CN/O=Home Security/C=US"

# Combine into PEM (mongoose expects this format)
cat server.crt server.key > $OUTPUT

# Clean up
rm server.key server.crt

echo "Certificate generated: $OUTPUT"
echo "Valid until: $(openssl x509 -in $OUTPUT -noout -enddate)"
echo ""
echo "To check expiration:"
echo "  openssl x509 -in $OUTPUT -noout -enddate"
```

---

## Certificate Update API Endpoint

### Implementation

```c
// In main/main.c
else if (mg_strcmp(hm->uri, mg_str("/api/cert/update")) == 0) {
    // 1. Verify admin authentication
    // 2. Parse multipart/form-data with certificate file
    // 3. Validate certificate format
    // 4. Save to SPIFFS: /spiffs/server.pem
    // 5. Restart HTTPS listener
    // 6. Return success/error
}
```

### Web UI Form

```html
<!-- In users.html or new cert.html -->
<form id="cert-upload" enctype="multipart/form-data">
    <input type="file" name="certificate" accept=".pem,.crt">
    <button type="submit">Update Certificate</button>
</form>
```

---

## Security Considerations

### Self-Signed Certificate Warnings

**What users will see:**
- Browser: "Your connection is not private"
- Click "Advanced" → "Proceed to site"
- One-time acceptance per browser

**Mitigation:**
- Document the warning in user manual
- Provide instructions for accepting certificate
- Consider pinning certificate in mobile apps

### Certificate Storage Security

**Recommendations:**
- Store in SPIFFS (not easily readable)
- Encrypt private key (optional, adds complexity)
- Don't log certificate contents
- Admin-only access to update endpoint

---

## Maintenance Schedule

### Year 0 (Now)
- Generate 10-year certificate
- Deploy to all devices
- Document expiration date

### Year 9 (Before Expiration)
- Generate new 10-year certificate
- Update devices via web UI or firmware update
- Test on one device first

### Ongoing
- Monitor certificate expiration (add to status page)
- Alert admin 90 days before expiration
- Keep backup of current certificate

---

## Comparison Table

| Solution | Renewal Needed | Complexity | Security | Maintenance |
|----------|---------------|------------|----------|-------------|
| **10-Year Self-Signed** | Every 10 years | Low | Medium | Very Low |
| **Web UI Update** | As needed | Medium | Medium | Low |
| **HTTP Only** | Never | Very Low | Low | None |
| **Reverse Proxy** | On router | High | High | Medium |
| **Auto-Generate** | Every 10 years | High | Medium | Low |

---

## Recommendation for Your Use Case

**For a home security system on local network:**

1. **Use 10-year self-signed certificate** (Option 1)
   - Generate once, embed in firmware
   - No renewal for 10 years
   - Acceptable for local network use

2. **Optional: Add certificate upload endpoint** (Option 2)
   - Allows updating without firmware flash
   - Useful if you want to switch to CA-signed cert later
   - Can be added later if needed

3. **Document expiration date**
   - Add to README: "Certificate expires: 2035-01-26"
   - Set calendar reminder for year 9
   - Add expiration check to status page

4. **Consider HTTP-only for initial deployment**
   - If HTTPS complexity is too much
   - Can add HTTPS later
   - Local network is relatively safe

---

## Implementation Steps

### Step 1: Generate Long-Lived Certificate

```bash
cd /home/kjgerhart/sentinel-esp
./scripts/generate_long_cert.sh
# Creates server.pem valid for 10 years
```

### Step 2: Embed in Firmware or SPIFFS

**Option A: Embed in firmware** (simpler)
- Include in build
- Access as embedded resource

**Option B: Store in SPIFFS** (more flexible)
- Upload during initial setup
- Can update later via web UI

### Step 3: Implement HTTPS (if desired)

- Add HTTPS listener
- Initialize TLS with certificate
- Test with browser

### Step 4: Add Expiration Monitoring (optional)

```c
// Check certificate expiration on startup
// Log warning if expires within 90 days
// Display on status page
```

---

## Conclusion

**Best Approach:** Generate a 10-year self-signed certificate and embed it in the firmware. This provides:
- ✅ 10 years without renewal
- ✅ Simple implementation
- ✅ Adequate security for local network
- ✅ No ongoing maintenance

**If you want flexibility:** Also implement the certificate upload endpoint so you can update certificates via web UI without firmware updates.

**If HTTPS is too complex:** Consider HTTP-only for local network use. The 2FA still provides security even without HTTPS on a trusted local network.

---

## Next Steps

1. Decide: HTTPS needed or HTTP sufficient?
2. If HTTPS: Generate 10-year certificate
3. If HTTPS: Implement HTTPS listener
4. Optional: Add certificate upload endpoint
5. Document expiration date and renewal process
