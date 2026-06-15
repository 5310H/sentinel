# Pre-Execution Checklist

## Before Running test_complete.sh

### ✅ Prerequisites

- [ ] `sentinel_test` server is running
  ```bash
  ps aux | grep sentinel_test | grep -v grep
  ```
  
- [ ] Server is listening on port 8000
  ```bash
  curl -s http://localhost:8000/ | head -1
  ```

- [ ] `curl` command is available
  ```bash
  which curl
  ```

- [ ] `jq` JSON parser is installed
  ```bash
  which jq
  ```

- [ ] `bash` shell available (should always be true)
  ```bash
  echo $BASH_VERSION
  ```

### ⚠️ System Status

- [ ] All zones are secure (or understand that ready=false)
- [ ] System is in DISARMED state (optional, script handles both)
- [ ] No real alarm is actively armed during test
- [ ] No sensors open/violated (so test can complete normally)

### 🔐 Security/Safety

- [ ] Test environment (localhost only, not production)
- [ ] No active emergency situations
- [ ] Comfortable with 2-second arm/disarm cycle
- [ ] Output will be visible on console (shows config info)

### 📋 Documentation

- [ ] Read `test.md` for what the tests do
- [ ] Read `CODE_REVIEW.md` for safety analysis
- [ ] Understand this is a read-mostly diagnostic tool
- [ ] Know that Arm/Disarm operations are included (brief, reversible)

---

## How to Run

### Option 1: Direct Execution
```bash
cd /home/kjgerhart/sentinel-esp/test_linux
./test_complete.sh
```

### Option 2: With Logging
```bash
cd /home/kjgerhart/sentinel-esp/test_linux
./test_complete.sh | tee test_results.log
```

### Option 3: From Any Directory
```bash
/home/kjgerhart/sentinel-esp/test_linux/test_complete.sh
```

---

## Expected Output Format

### SUCCESS (All tests pass)
```
════════════════════════════════════════
SECTION 1: Server Connectivity
════════════════════════════════════════
✓ PASS: Server is running on http://localhost:8000
✓ PASS: Main page (index.html) loads

════════════════════════════════════════
SECTION 2: System Status Endpoints
════════════════════════════════════════
✓ PASS: /api/status returns config object
  System State: 0, Ready: true
✓ PASS: /api/status returns zones array (8 zones)
...

════════════════════════════════════════
TEST SUMMARY
════════════════════════════════════════
Passed: 30
Failed: 0
Warnings: 0
Total Tests: 30

✓ ALL CRITICAL TESTS PASSED
```

Exit code: `0`

### WITH WARNINGS (Tests pass but issues detected)
```
...
⚠ WARN: System is NOT READY (open zones or violations)
⚠ WARN: Account ID not set
...

Passed: 28
Failed: 0
Warnings: 2
Total Tests: 30

✓ ALL CRITICAL TESTS PASSED
```

Exit code: `0` (warnings don't fail)

### WITH FAILURES (Tests failed)
```
...
✗ FAIL: /api/status does not contain config
✗ FAIL: Invalid auth endpoint response
...

Passed: 15
Failed: 5
Warnings: 0
Total Tests: 20

✗ SOME TESTS FAILED
```

Exit code: `1`

---

## What Each Section Tests

| Section | Endpoint | Type | Impact | Duration |
|---------|----------|------|--------|----------|
| 1 | GET `/` | Read | None | <100ms |
| 2 | GET `/api/status` | Read | None | <100ms |
| 2 | GET `/status` | Read | None | <100ms |
| 3 | POST `/api/auth` | Read | None | <100ms |
| 4 | POST `/api/arm` | **Write** | Arms system | <100ms |
| 4 | POST `/api/disarm` | **Write** | Disarms system | <100ms |
| 5 | GET `/api/zones` | Read | None | <100ms |
| 6 | GET `/*.html` | Read | None | <100ms |
| 7 | GET `/api/status` | Read | None | <100ms |
| 8 | GET `/api/status` | Read | None | <100ms |
| 9 | GET `/api/status` | Read | None | <100ms |

**Total Write Operations**: 2 (arm + disarm, takes ~2 seconds total)
**Total Read Operations**: 20+
**Total Duration**: ~3-4 seconds

---

## Troubleshooting

### ❌ "Server is not running"
```bash
# Start sentinel_test
cd /home/kjgerhart/sentinel-esp/test_linux
./sentinel_test
# Keep running in background or different terminal
```

### ❌ "command not found: jq"
```bash
# Install jq on Linux
sudo apt install jq

# Or on macOS
brew install jq
```

### ❌ "command not found: curl"
```bash
# Install curl
sudo apt install curl
```

### ⚠️ "System is NOT READY"
- This is a WARNING, not a failure
- Means some zones are open or violated
- Script continues normally
- Expected if a door/window is actually open

### ⚠️ "Invalid PIN (9999) was NOT rejected"
- This is a WARNING, indicates security issue
- PIN validation may not be working
- Investigate authentication in main_mock.c

### ✗ "ARM endpoint response invalid"
- Check `/api/arm` implementation in main_mock.c
- Verify response is valid JSON with `{"status": "ok"}`

---

## Post-Execution

### After Script Finishes

1. **Check Exit Code**
   ```bash
   echo $?
   # 0 = Success, 1 = Failure
   ```

2. **Review Output**
   - Look for ✓ PASS results
   - Note any ⚠ WARN items
   - Note any ✗ FAIL items

3. **Check System State**
   ```bash
   curl -s http://localhost:8000/api/status | jq '.config.state'
   # Should be 0 (Disarmed) after script finishes
   ```

4. **If Logging**
   ```bash
   cat test_results.log
   ```

5. **If Tests Failed**
   - Review CODE_REVIEW.md safety section
   - Check sentinel_test console output for errors
   - Review main_mock.c for endpoint implementation

---

## Quick Validation

Run this BEFORE executing test_complete.sh:

```bash
# All should return success (exit code 0)

# Test 1: Server responds
curl -s http://localhost:8000/ > /dev/null && echo "✓ Server OK"

# Test 2: API endpoint works
curl -s http://localhost:8000/api/status | jq . > /dev/null && echo "✓ API OK"

# Test 3: curl installed
curl --version | head -1

# Test 4: jq installed
jq --version

# Test 5: Script is executable
test -x /home/kjgerhart/sentinel-esp/test_linux/test_complete.sh && echo "✓ Script executable"
```

If all 5 tests pass ✓, you're ready to run test_complete.sh

---

## Summary

✅ **SAFE**: Non-destructive diagnostic tool
⚠️ **BRIEF**: 2-second arm/disarm cycle
✓ **QUICK**: ~3-4 seconds to complete
📊 **INFORMATIVE**: Comprehensive test results
🔄 **REPEATABLE**: Can run multiple times safely

**You are cleared to run!** 🚀
