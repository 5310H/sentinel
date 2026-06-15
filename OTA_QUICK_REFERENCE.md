# OTA Quick Reference Card

## ⚡ Quick Update Command

```bash
# Upload firmware with master PIN
curl -X POST \
  -H "Authorization: Bearer YOUR_PIN" \
  -F "file=@build/sentinel-esp.bin" \
  https://sentinel.local/api/ota/upload
```

## 🔐 Authentication Options

| Method | Header Format |
|--------|---------------|
| **Master PIN** | `Authorization: Bearer 1234` |
| **User PIN** | `Authorization: Bearer user_pin_here` |

## ✅ Success Response

```json
{
  "status": "success",
  "message": "Firmware updated, restarting in 3 seconds",
  "bytes": 873216
}
```

Device restarts automatically after 3 seconds.

## ❌ Common Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `401 Unauthorized` | Invalid PIN | Check PIN in config.json |
| `400 Invalid firmware` | Wrong file format | Upload `build/sentinel-esp.bin` |
| `500 OTA partition` | No space | Check partition table |

## 🛠️ Build & Deploy

```bash
# 1. Build firmware
cd /home/kjgerhart/sentinel-esp
idf.py build

# 2. Upload via OTA
curl -X POST \
  -H "Authorization: Bearer 1234" \
  -F "file=@build/sentinel-esp.bin" \
  https://sentinel.local/api/ota/upload

# 3. Wait for restart (3-60 seconds)
ping -c 10 sentinel.local
```

## 🚨 Emergency Rollback

If new firmware doesn't boot:

```bash
# Option 1: Automatic (after 3 failed boots)
# Device rolls back automatically

# Option 2: Manual via USB
cd /home/kjgerhart/sentinel-esp
idf.py flash monitor
```

## 📊 Validation Checks

The system validates before flashing:

- ✓ Magic number (0xE9)
- ✓ Image header structure
- ✓ File size (< 10MB)
- ✓ Partition space available

## 🔒 Security Features

- **HTTPS**: All uploads encrypted (TLS 1.2+)
- **PIN Auth**: Master or user PIN required
- **Atomic**: Rollback on failure
- **Validated**: Checks firmware format before writing

## 📖 Full Documentation

See [OTA_SECURE_UPDATE_GUIDE.md](OTA_SECURE_UPDATE_GUIDE.md) for:
- Firmware signing
- CI/CD integration
- Rate limiting
- Audit logging
- Version management

---

**Endpoint:** `https://sentinel.local/api/ota/upload`  
**Max Size:** 10MB  
**Format:** ESP32 application binary (`.bin`)
