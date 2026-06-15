# Phase 2: MJPEG Streaming Implementation

**Date:** January 31, 2026  
**Goal:** Add MJPEG streaming proxy for smoother live camera viewing (10+ FPS)

---

## Architecture Overview

### Current (Phase 1): Snapshot Polling
```
Browser ──(every 1s)──> Sentinel ──HTTP GET──> Thingino /image.jpg
                         ↓
                    Returns JPEG
```
- **FPS:** 1-2 FPS
- **Latency:** 1000ms
- **Bandwidth:** 100KB/s per camera

### Phase 2: MJPEG Streaming Proxy
```
Browser ──(continuous)──> Sentinel MJPEG Proxy ──HTTP GET──> Thingino /mjpeg
                           ↓                                      ↓
                      Multipart/x-mixed-replace           Continuous JPEG frames
```
- **FPS:** 10-15 FPS (configurable)
- **Latency:** 100-200ms
- **Bandwidth:** 500KB-1MB/s per camera

---

## MJPEG Streaming Format

MJPEG (Motion JPEG) is a simple streaming format where JPEG images are sent continuously over HTTP using `multipart/x-mixed-replace`.

### HTTP Response Format
```
HTTP/1.1 200 OK
Content-Type: multipart/x-mixed-replace; boundary=frame

--frame
Content-Type: image/jpeg
Content-Length: 65432

<JPEG DATA>
--frame
Content-Type: image/jpeg
Content-Length: 64891

<JPEG DATA>
--frame
...
```

### Browser Support
- ✅ **Chrome/Edge:** Native support (`<img src="url">`)
- ✅ **Firefox:** Native support
- ✅ **Safari:** Native support
- ✅ **Mobile:** Works on iOS/Android browsers

---

## Implementation Strategy

### Option 1: Direct Proxy (RECOMMENDED)
**Sentinel acts as pass-through proxy for Thingino MJPEG stream**

**Pros:**
- Simple implementation (~100 lines C)
- Low CPU usage on ESP32
- No frame buffering needed
- Real-time streaming

**Cons:**
- Requires Thingino to have MJPEG endpoint
- One client per camera (no multicast)

### Option 2: Frame Capture + Re-encode
**Sentinel fetches snapshots and encodes MJPEG stream**

**Pros:**
- Works with cameras that only have snapshot endpoint
- Can serve multiple clients
- Frame rate control

**Cons:**
- High CPU usage (JPEG encoding)
- More complex code (~500 lines)
- Memory intensive

**Decision:** Use **Option 1 (Direct Proxy)** for Phase 2

---

## Thingino MJPEG Endpoint Verification

Thingino cameras typically provide these endpoints:

| Endpoint | Description | FPS | Quality |
|----------|-------------|-----|---------|
| `/image.jpg` | Single snapshot | On-demand | High |
| `/mjpeg` | MJPEG stream | 10-15 | Medium |
| `/video` | H.264 stream | 25-30 | Best |

**Testing:**
```bash
# Check if Thingino supports MJPEG
curl -I http://192.168.1.101/mjpeg

# Expected response:
HTTP/1.1 200 OK
Content-Type: multipart/x-mixed-replace; boundary=frame
```

---

## Implementation Plan

### Step 1: Add MJPEG Proxy Endpoint
**File:** `main/main.c`

```c
/**
 * MJPEG streaming proxy handler
 * GET /api/camera/:id/stream
 */
static void handle_camera_mjpeg_stream(struct mg_connection *c, 
                                        struct mg_http_message *hm) {
    // Parse camera ID from URL: /api/camera/0/stream
    int camera_id = -1;
    sscanf(hm->uri.buf, "/api/camera/%d/stream", &camera_id);
    
    const camera_device_t *cam = camera_mgr_get_device(camera_id);
    
    if (!cam || !cam->enabled) {
        mg_http_reply(c, 404, "", "{\"error\":\"Camera not found\"}");
        return;
    }
    
    // Build MJPEG URL for camera
    char mjpeg_url[256];
    snprintf(mjpeg_url, sizeof(mjpeg_url), "http://%s:%d/mjpeg", 
             cam->hostname, cam->port);
    
    ESP_LOGI(TAG, "Proxying MJPEG stream from %s", mjpeg_url);
    
    // Send MJPEG headers to browser
    mg_printf(c, "HTTP/1.1 200 OK\r\n"
                 "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                 "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                 "Pragma: no-cache\r\n"
                 "Expires: 0\r\n"
                 "\r\n");
    
    // Mark connection as streaming (don't close)
    c->is_resp = 0;
    
    // Create upstream connection to Thingino camera
    struct mg_connection *upstream = mg_http_connect(
        &mg_mgr, mjpeg_url, handle_mjpeg_upstream_response, c
    );
    
    if (!upstream) {
        ESP_LOGE(TAG, "Failed to connect to camera MJPEG stream");
        c->is_draining = 1;
        return;
    }
    
    // Send HTTP GET request to camera
    mg_printf(upstream, "GET /mjpeg HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "Connection: keep-alive\r\n"
                        "\r\n", cam->hostname);
}

/**
 * Handle upstream MJPEG response from camera
 */
static void handle_mjpeg_upstream_response(struct mg_connection *upstream, 
                                            int ev, void *ev_data) {
    if (ev == MG_EV_READ) {
        // Get client connection (stored in upstream->fn_data)
        struct mg_connection *client = (struct mg_connection *)upstream->fn_data;
        
        if (client && !client->is_draining) {
            // Forward MJPEG data directly to browser
            mg_send(client, upstream->recv.buf, upstream->recv.len);
        }
        
        // Clear upstream buffer
        upstream->recv.len = 0;
        
    } else if (ev == MG_EV_CLOSE) {
        // Upstream closed, close client connection
        struct mg_connection *client = (struct mg_connection *)upstream->fn_data;
        if (client) {
            client->is_draining = 1;
        }
    }
}
```

### Step 2: Update Frontend to Support MJPEG
**File:** `data/cameras_live.html`

Add toggle for snapshot vs MJPEG mode:

```html
<div class="controls">
    <label>
        <input type="checkbox" id="mjpeg-toggle" checked>
        Use MJPEG Streaming (smoother, higher bandwidth)
    </label>
    <button id="refresh-btn" onclick="refreshAll()">🔄 Refresh</button>
    <div class="grid-selector">
        <button onclick="setGridSize(1)">1×1</button>
        <button onclick="setGridSize(2)" class="active">2×2</button>
        <button onclick="setGridSize(3)">3×3</button>
        <button onclick="setGridSize(4)">4×4</button>
    </div>
</div>
```

Update JavaScript:

```javascript
// Check if MJPEG streaming is enabled
function useMJPEG() {
    return document.getElementById('mjpeg-toggle').checked;
}

// Load camera in tile
function loadCameraImage(camera, index) {
    const img = document.getElementById(`camera-${index}`);
    
    if (useMJPEG()) {
        // Use MJPEG streaming (continuous)
        img.src = `/api/camera/${index}/stream`;
        img.onerror = () => {
            // Fallback to snapshot polling if MJPEG fails
            console.warn(`MJPEG not available for camera ${index}, using snapshots`);
            img.src = `/api/camera/${index}/snapshot?${Date.now()}`;
        };
    } else {
        // Use snapshot polling (1 FPS)
        img.src = `/api/camera/${index}/snapshot?${Date.now()}`;
    }
}

// Handle MJPEG toggle change
document.getElementById('mjpeg-toggle').addEventListener('change', () => {
    // Reload all cameras with new mode
    cameras.forEach((camera, index) => {
        loadCameraImage(camera, index);
    });
});
```

### Step 3: Add Connection Management
**Challenge:** ESP32 has limited connections (default 5-10)

**Solution:** Connection pooling + timeouts

```c
// Add to camera_mgr.h
#define MAX_MJPEG_STREAMS 4  // Max simultaneous MJPEG streams

typedef struct {
    int camera_id;
    struct mg_connection *client_conn;
    struct mg_connection *upstream_conn;
    time_t start_time;
    size_t bytes_transferred;
} mjpeg_stream_t;

static mjpeg_stream_t active_streams[MAX_MJPEG_STREAMS];

/**
 * Register new MJPEG stream
 */
static int register_mjpeg_stream(int camera_id, struct mg_connection *client) {
    // Find free slot
    for (int i = 0; i < MAX_MJPEG_STREAMS; i++) {
        if (!active_streams[i].client_conn) {
            active_streams[i].camera_id = camera_id;
            active_streams[i].client_conn = client;
            active_streams[i].start_time = time(NULL);
            active_streams[i].bytes_transferred = 0;
            return i;
        }
    }
    return -1; // No free slots
}

/**
 * Cleanup idle/closed streams
 */
static void cleanup_mjpeg_streams(void) {
    time_t now = time(NULL);
    
    for (int i = 0; i < MAX_MJPEG_STREAMS; i++) {
        mjpeg_stream_t *stream = &active_streams[i];
        
        if (stream->client_conn) {
            // Check if connection is closed
            if (stream->client_conn->is_draining || stream->client_conn->is_closing) {
                ESP_LOGI(TAG, "Cleaning up MJPEG stream %d (closed)", i);
                
                // Close upstream connection
                if (stream->upstream_conn) {
                    stream->upstream_conn->is_draining = 1;
                }
                
                // Clear slot
                memset(stream, 0, sizeof(mjpeg_stream_t));
            }
            // Check for timeout (5 minutes idle)
            else if (now - stream->start_time > 300) {
                ESP_LOGI(TAG, "Cleaning up MJPEG stream %d (timeout)", i);
                stream->client_conn->is_draining = 1;
                if (stream->upstream_conn) {
                    stream->upstream_conn->is_draining = 1;
                }
                memset(stream, 0, sizeof(mjpeg_stream_t));
            }
        }
    }
}
```

### Step 4: Add Tests
**File:** `test_linux/test_complete.sh`

```bash
# Test MJPEG endpoint exists
echo "Testing MJPEG streaming endpoint..."
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:8000/api/camera/0/stream)
if [ "$RESPONSE" = "200" ] || [ "$RESPONSE" = "404" ]; then
    echo "✓ MJPEG endpoint responds (HTTP $RESPONSE)"
else
    echo "✗ MJPEG endpoint failed (HTTP $RESPONSE)"
    FAILED=$((FAILED + 1))
fi
```

---

## Performance Considerations

### Bandwidth Usage
| Cameras | Snapshot (1 FPS) | MJPEG (10 FPS) |
|---------|------------------|----------------|
| 1 camera | 100 KB/s | 500 KB/s |
| 2 cameras | 200 KB/s | 1 MB/s |
| 4 cameras | 400 KB/s | 2 MB/s |
| 8 cameras | 800 KB/s | 4 MB/s |

### ESP32 Resources
- **RAM:** ~16KB per stream (buffer + metadata)
- **CPU:** <5% (pass-through proxy)
- **Connections:** 1 upstream + 1 client per stream
- **Max Streams:** 4 concurrent (configurable)

### Network Requirements
- **Local LAN:** No issues (100 Mbps+)
- **WiFi:** 802.11n/ac recommended (54+ Mbps)
- **Remote Access:** May require bandwidth throttling

---

## User Controls

### Grid View Settings
```
┌─────────────────────────────────────────┐
│ Sentinel - Live Camera View             │
├─────────────────────────────────────────┤
│ ☑ Use MJPEG Streaming (smoother)        │
│   (Uncheck for slower connections)      │
│                                          │
│ Grid: [1×1] [2×2] [3×3] [4×4]            │
│                                          │
│ ┌────────┐  ┌────────┐  ┌────────┐      │
│ │Cam 1   │  │Cam 2   │  │Cam 3   │      │
│ │🟢 MJPEG│  │🟢 MJPEG│  │🟢 10 FPS│     │
│ └────────┘  └────────┘  └────────┘      │
└─────────────────────────────────────────┘
```

### Status Indicators
- **🟢 Green:** MJPEG streaming active
- **🔵 Blue:** Snapshot polling mode
- **🔴 Red:** Camera offline/error

---

## Fallback Strategy

If Thingino camera doesn't support `/mjpeg`:

1. **Try `/mjpeg`** → 404 Not Found
2. **Fallback to snapshot polling** → Use `/image.jpg` at 1 FPS
3. **Show banner:** "MJPEG not available, using snapshots"

**Detection Code:**
```javascript
img.onerror = () => {
    // MJPEG failed, fallback to snapshots
    console.warn(`MJPEG not available for camera ${index}`);
    useMJPEGForCamera[index] = false; // Remember fallback
    img.src = `/api/camera/${index}/snapshot?${Date.now()}`;
};
```

---

## Testing Checklist

### Unit Tests
- [ ] Parse camera ID from `/api/camera/0/stream`
- [ ] Handle invalid camera ID (404)
- [ ] Handle disabled camera (404)
- [ ] Handle upstream connection failure

### Integration Tests
- [ ] MJPEG proxy connects to Thingino
- [ ] Frames forwarded to browser correctly
- [ ] Multiple clients can stream simultaneously
- [ ] Connection cleanup on client disconnect
- [ ] Bandwidth usage within limits
- [ ] CPU usage <10% with 4 streams

### Browser Tests
- [ ] Chrome: `<img src="/api/camera/0/stream">` displays video
- [ ] Firefox: MJPEG displays correctly
- [ ] Safari: MJPEG works on iOS
- [ ] Toggle between MJPEG and snapshots works
- [ ] Fallback to snapshots if MJPEG unavailable

### Load Tests
- [ ] 1 client, 4 cameras streaming (4 streams)
- [ ] 4 clients, 1 camera each (4 streams)
- [ ] ESP32 doesn't crash or reboot
- [ ] Connection limit enforced (max 4 streams)

---

## Implementation Timeline

### Day 1 (Today)
- [x] Design MJPEG architecture
- [ ] Implement MJPEG proxy endpoint in main.c
- [ ] Add connection management
- [ ] Test with single camera

### Day 2
- [ ] Update cameras_live.html with MJPEG toggle
- [ ] Add fallback logic for non-MJPEG cameras
- [ ] Test multi-camera streaming
- [ ] Add cleanup/timeout logic

### Day 3
- [ ] Add Linux mock MJPEG endpoint
- [ ] Add automated tests
- [ ] Performance testing
- [ ] Documentation

---

## Known Limitations

1. **Max 4 Concurrent Streams:** ESP32 connection limit
2. **No Recording Yet:** MJPEG streams are not saved (Phase 3)
3. **Bandwidth Intensive:** 10x more bandwidth than snapshots
4. **Requires MJPEG Endpoint:** Cameras must support `/mjpeg`
5. **No Multicast:** Each client needs separate stream

---

## Success Criteria

✅ **Phase 2 Complete When:**
- [ ] `/api/camera/:id/stream` endpoint works
- [ ] MJPEG streams display in browser at 10+ FPS
- [ ] Toggle between MJPEG and snapshots functional
- [ ] Fallback to snapshots if MJPEG unavailable
- [ ] Connection pooling limits concurrent streams
- [ ] Tests pass for ESP32 and Linux mock
- [ ] Documentation updated

---

**Status:** 🔨 IN PROGRESS

**Next Action:** Implement MJPEG proxy handler in [main/main.c](main/main.c)
