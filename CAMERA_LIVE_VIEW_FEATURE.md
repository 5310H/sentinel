# Live Camera Grid View Feature

**Status:** Proposed Enhancement  
**Priority:** High ⭐  
**Estimated Effort:** 1-2 days  
**Date:** January 31, 2026

---

## Overview

Add a web-based live camera grid viewer to Sentinel that displays multiple camera feeds in a grid layout with click-to-fullscreen capability. This eliminates the need for external NVR software (like Blue Iris which is Windows-only) for basic live monitoring.

---

## User Experience

### Grid View
```
┌─────────────────────────────────────────────────┐
│  Sentinel - Live Camera View            [Grid ▼]│
├─────────────────────────────────────────────────┤
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────┐│
│  │ Camera 1│  │ Camera 2│  │ Camera 3│  │ Cam4││
│  │ Backyard│  │Front Dr.│  │ Garage  │  │Side ││
│  │ 🟢 Live │  │ 🟢 Live │  │ 🔴 Off  │  │🟢Live││
│  └─────────┘  └─────────┘  └─────────┘  └─────┘│
│  ┌─────────┐  ┌─────────┐                       │
│  │ Camera 5│  │ Camera 6│                       │
│  │ Driveway│  │Floodlgt.│                       │
│  │ 🟢 Live │  │ 🟢 Live │                       │
│  └─────────┘  └─────────┘                       │
│                                                  │
│  [⬜ 1x1] [▦ 2x2] [▦ 3x3] [▦ 4x4]  [⟳ Refresh] │
└─────────────────────────────────────────────────┘
```

### Fullscreen View (After Click)
```
┌─────────────────────────────────────────────────┐
│  Backyard Camera                      [✕ Close] │
├─────────────────────────────────────────────────┤
│                                                  │
│                                                  │
│              LIVE CAMERA FEED                   │
│               1920x1080                          │
│                                                  │
│                                                  │
├─────────────────────────────────────────────────┤
│ 🟢 Live  │ 📷 Snapshot │ 🔴 Record │ ⚙️ Settings│
└─────────────────────────────────────────────────┘
```

---

## Implementation Options

### Option 1: Auto-Refresh Snapshots (EASIEST)
**Pros:** Simple, works with existing code, low bandwidth  
**Cons:** ~1-2 second delay, not true "live"

```javascript
// Auto-refresh JPEG snapshots every 1 second
setInterval(() => {
    document.getElementById('camera-0').src = 
        '/api/camera/0/snapshot?' + Date.now();
}, 1000);
```

### Option 2: MJPEG Streaming (RECOMMENDED)
**Pros:** Smooth video, standard protocol, low CPU  
**Cons:** Requires MJPEG endpoint on Thingino

```html
<!-- Native browser MJPEG support -->
<img src="http://192.168.1.101/mjpeg" />
```

### Option 3: WebRTC (ADVANCED)
**Pros:** True real-time, low latency (<500ms)  
**Cons:** Complex, requires WebRTC server

### Option 4: HLS Streaming (BEST QUALITY)
**Pros:** Adaptive bitrate, mobile-friendly  
**Cons:** Requires transcoding, higher latency

---

## Proposed Implementation

### File Structure
```
data/
  cameras_live.html     # Main live view page
  js/
    camera_grid.js      # Grid layout logic
    camera_player.js    # Fullscreen player
  css/
    camera_grid.css     # Responsive grid styles
```

### Backend API (C - Mongoose)

Add to `main/main.c`:

```c
/**
 * Serve live camera grid page
 */
static void handle_cameras_live(struct mg_connection *c, struct mg_http_message *hm) {
    mg_http_serve_file(c, hm, "/spiffs/cameras_live.html", &mg_http_serve_opts);
}

/**
 * Stream MJPEG from camera (proxy)
 */
static void handle_camera_stream(struct mg_connection *c, struct mg_http_message *hm) {
    // Parse camera ID from URL: /api/camera/0/stream
    int camera_id = get_camera_id_from_url(hm->uri);
    const camera_device_t *cam = camera_mgr_get_device(camera_id);
    
    if (!cam || !cam->enabled) {
        mg_http_reply(c, 404, "", "Camera not found");
        return;
    }
    
    // Send MJPEG headers
    mg_printf(c, "HTTP/1.1 200 OK\r\n"
                 "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                 "Cache-Control: no-cache\r\n"
                 "Connection: close\r\n\r\n");
    
    // Stream loop (runs until client disconnects)
    while (c->is_writable) {
        uint8_t *buffer = NULL;
        size_t size = 0;
        
        if (camera_mgr_fetch_snapshot(camera_id, &buffer, &size) == ESP_OK) {
            mg_printf(c, "--frame\r\n"
                         "Content-Type: image/jpeg\r\n"
                         "Content-Length: %zu\r\n\r\n", size);
            mg_send(c, buffer, size);
            mg_send(c, "\r\n", 2);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 10 FPS
    }
}

// Register routes
mg_http_listen(&mgr, "http://0.0.0.0:443", fn, NULL);
mg_http_handler("/cameras_live.html", handle_cameras_live);
mg_http_handler("/api/camera/*/stream", handle_camera_stream);
```

### Frontend HTML (`data/cameras_live.html`)

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Sentinel - Live Camera View</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { 
            font-family: Arial, sans-serif; 
            background: #1a1a1a; 
            color: #fff;
        }
        
        .header {
            background: #2c3e50;
            padding: 15px;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        
        .camera-grid {
            display: grid;
            gap: 10px;
            padding: 10px;
            height: calc(100vh - 60px);
        }
        
        .grid-2x2 { grid-template-columns: repeat(2, 1fr); }
        .grid-3x3 { grid-template-columns: repeat(3, 1fr); }
        .grid-4x4 { grid-template-columns: repeat(4, 1fr); }
        
        .camera-tile {
            position: relative;
            background: #000;
            border: 2px solid #444;
            border-radius: 8px;
            overflow: hidden;
            cursor: pointer;
            transition: transform 0.2s;
        }
        
        .camera-tile:hover {
            transform: scale(1.02);
            border-color: #3498db;
        }
        
        .camera-tile img {
            width: 100%;
            height: 100%;
            object-fit: cover;
        }
        
        .camera-overlay {
            position: absolute;
            bottom: 0;
            left: 0;
            right: 0;
            background: rgba(0,0,0,0.7);
            padding: 10px;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        
        .camera-name {
            font-weight: bold;
            font-size: 14px;
        }
        
        .camera-status {
            display: flex;
            align-items: center;
            gap: 5px;
        }
        
        .status-dot {
            width: 10px;
            height: 10px;
            border-radius: 50%;
            animation: pulse 2s infinite;
        }
        
        .status-dot.online { background: #2ecc71; }
        .status-dot.offline { background: #e74c3c; animation: none; }
        
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; }
        }
        
        /* Fullscreen modal */
        .fullscreen-modal {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            width: 100vw;
            height: 100vh;
            background: rgba(0,0,0,0.95);
            z-index: 1000;
        }
        
        .fullscreen-modal.active { display: flex; flex-direction: column; }
        
        .modal-header {
            background: #2c3e50;
            padding: 15px;
            display: flex;
            justify-content: space-between;
        }
        
        .modal-content {
            flex: 1;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
        }
        
        .modal-content img {
            max-width: 100%;
            max-height: 100%;
            object-fit: contain;
        }
        
        .modal-controls {
            background: #34495e;
            padding: 15px;
            display: flex;
            gap: 15px;
            justify-content: center;
        }
        
        button {
            background: #3498db;
            color: white;
            border: none;
            padding: 10px 20px;
            border-radius: 5px;
            cursor: pointer;
            font-size: 14px;
        }
        
        button:hover { background: #2980b9; }
        button:active { transform: scale(0.98); }
    </style>
</head>
<body>
    <!-- Header -->
    <div class="header">
        <h1>📹 Sentinel - Live Camera View</h1>
        <div>
            <button onclick="setGridSize(2)">2×2</button>
            <button onclick="setGridSize(3)">3×3</button>
            <button onclick="setGridSize(4)">4×4</button>
            <button onclick="refreshAll()">🔄 Refresh</button>
        </div>
    </div>
    
    <!-- Camera Grid -->
    <div id="camera-grid" class="camera-grid grid-2x2"></div>
    
    <!-- Fullscreen Modal -->
    <div id="fullscreen-modal" class="fullscreen-modal">
        <div class="modal-header">
            <h2 id="modal-camera-name">Camera Name</h2>
            <button onclick="closeFullscreen()">✕ Close</button>
        </div>
        <div class="modal-content">
            <img id="fullscreen-image" src="" alt="Camera Feed">
        </div>
        <div class="modal-controls">
            <button onclick="takeSnapshot()">📷 Snapshot</button>
            <button onclick="toggleRecording()">🔴 Record</button>
            <button onclick="openSettings()">⚙️ Settings</button>
        </div>
    </div>
    
    <script>
        let cameras = [];
        let gridSize = 2;
        let refreshInterval = null;
        let currentCamera = null;
        
        // Load cameras from API
        async function loadCameras() {
            try {
                const response = await fetch('/api/cameras');
                cameras = await response.json();
                renderGrid();
                startAutoRefresh();
            } catch (error) {
                console.error('Failed to load cameras:', error);
            }
        }
        
        // Render camera grid
        function renderGrid() {
            const grid = document.getElementById('camera-grid');
            grid.innerHTML = '';
            
            cameras.forEach((camera, index) => {
                const tile = document.createElement('div');
                tile.className = 'camera-tile';
                tile.onclick = () => openFullscreen(camera, index);
                
                tile.innerHTML = `
                    <img id="camera-${index}" 
                         src="/api/camera/${index}/snapshot?${Date.now()}"
                         onerror="this.src='/camera_offline.jpg'"
                         alt="${camera.friendly_name}">
                    <div class="camera-overlay">
                        <span class="camera-name">${camera.friendly_name}</span>
                        <div class="camera-status">
                            <span class="status-dot ${camera.enabled ? 'online' : 'offline'}"></span>
                            <span>${camera.enabled ? 'Live' : 'Offline'}</span>
                        </div>
                    </div>
                `;
                
                grid.appendChild(tile);
            });
        }
        
        // Set grid size
        function setGridSize(size) {
            gridSize = size;
            const grid = document.getElementById('camera-grid');
            grid.className = `camera-grid grid-${size}x${size}`;
        }
        
        // Auto-refresh snapshots
        function startAutoRefresh() {
            if (refreshInterval) clearInterval(refreshInterval);
            
            refreshInterval = setInterval(() => {
                cameras.forEach((camera, index) => {
                    if (camera.enabled) {
                        const img = document.getElementById(`camera-${index}`);
                        if (img) {
                            img.src = `/api/camera/${index}/snapshot?${Date.now()}`;
                        }
                    }
                });
            }, 1000); // Refresh every 1 second
        }
        
        // Manual refresh all
        function refreshAll() {
            cameras.forEach((camera, index) => {
                const img = document.getElementById(`camera-${index}`);
                if (img) {
                    img.src = `/api/camera/${index}/snapshot?${Date.now()}`;
                }
            });
        }
        
        // Open fullscreen view
        function openFullscreen(camera, index) {
            currentCamera = { camera, index };
            const modal = document.getElementById('fullscreen-modal');
            const image = document.getElementById('fullscreen-image');
            const name = document.getElementById('modal-camera-name');
            
            name.textContent = camera.friendly_name;
            
            // Use MJPEG stream if available, otherwise use snapshot polling
            if (camera.mjpeg_supported) {
                image.src = `/api/camera/${index}/stream`;
            } else {
                image.src = `/api/camera/${index}/snapshot?${Date.now()}`;
                startFullscreenRefresh(index);
            }
            
            modal.classList.add('active');
        }
        
        // Close fullscreen
        function closeFullscreen() {
            const modal = document.getElementById('fullscreen-modal');
            modal.classList.remove('active');
            if (window.fullscreenRefreshInterval) {
                clearInterval(window.fullscreenRefreshInterval);
            }
            currentCamera = null;
        }
        
        // Refresh fullscreen snapshot
        function startFullscreenRefresh(cameraId) {
            if (window.fullscreenRefreshInterval) {
                clearInterval(window.fullscreenRefreshInterval);
            }
            
            window.fullscreenRefreshInterval = setInterval(() => {
                const image = document.getElementById('fullscreen-image');
                if (image && !image.src.includes('/stream')) {
                    image.src = `/api/camera/${cameraId}/snapshot?${Date.now()}`;
                }
            }, 500); // 2 FPS for fullscreen
        }
        
        // Take snapshot
        async function takeSnapshot() {
            if (!currentCamera) return;
            
            const link = document.createElement('a');
            link.href = `/api/camera/${currentCamera.index}/snapshot`;
            link.download = `${currentCamera.camera.friendly_name}_${Date.now()}.jpg`;
            link.click();
        }
        
        // Toggle recording (placeholder)
        function toggleRecording() {
            alert('Recording feature coming soon!');
        }
        
        // Open camera settings
        function openSettings() {
            if (currentCamera) {
                window.location.href = `/cameras.html?edit=${currentCamera.index}`;
            }
        }
        
        // Keyboard shortcuts
        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') closeFullscreen();
            if (e.key === 'f' || e.key === 'F') {
                if (document.getElementById('fullscreen-modal').classList.contains('active')) {
                    document.getElementById('fullscreen-image').requestFullscreen();
                }
            }
        });
        
        // Initialize on page load
        loadCameras();
    </script>
</body>
</html>
```

---

## Integration Steps

### 1. Add Route to Mongoose Server
```c
// In main.c
mg_http_handler("/cameras_live.html", handle_cameras_live);
mg_http_handler("/api/camera/*/stream", handle_camera_stream); // MJPEG
```

### 2. Update Navigation Menu
Add link to `data/index.html`:
```html
<nav>
    <a href="/">Dashboard</a>
    <a href="/cameras.html">Camera Config</a>
    <a href="/cameras_live.html">📹 Live View</a>  <!-- NEW -->
    <a href="/zones.html">Zones</a>
</nav>
```

### 3. Add MJPEG Stream Support (Optional)
If Thingino doesn't provide native MJPEG endpoint, create proxy:

```c
// Fetch and stream JPEGs in loop
void handle_camera_stream(struct mg_connection *c, struct mg_http_message *hm) {
    int camera_id = parse_camera_id(hm->uri);
    
    mg_printf(c, "HTTP/1.1 200 OK\r\n"
                 "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n");
    
    while (c->is_writable) {
        uint8_t *buffer;
        size_t size;
        
        if (camera_mgr_fetch_snapshot(camera_id, &buffer, &size) == ESP_OK) {
            mg_printf(c, "--frame\r\n"
                         "Content-Type: image/jpeg\r\n"
                         "Content-Length: %zu\r\n\r\n", size);
            mg_send(c, buffer, size);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 10 FPS
    }
}
```

---

## Features

### Core Functionality ✅
- [x] Grid view (2×2, 3×3, 4×4)
- [x] Auto-refresh snapshots (1 FPS)
- [x] Click to fullscreen
- [x] Camera status indicators
- [x] Manual refresh button
- [x] Keyboard shortcuts (ESC to close, F for fullscreen)

### Advanced Features (Optional)
- [ ] MJPEG streaming (requires camera support)
- [ ] PTZ controls (pan/tilt/zoom)
- [ ] Digital zoom (pinch/scroll)
- [ ] Snapshot download
- [ ] Recording to SD card
- [ ] Motion detection overlay
- [ ] Alarm event markers
- [ ] Multi-monitor support

---

## Performance Considerations

### Bandwidth Usage
- **2×2 Grid (4 cameras):** ~400KB/s (100KB/cam @ 1 FPS)
- **3×3 Grid (9 cameras):** ~900KB/s
- **4×4 Grid (16 cameras):** ~1.6MB/s
- **Fullscreen MJPEG:** ~500KB/s @ 10 FPS

### ESP32 Memory
- Grid view: Minimal (snapshots fetched on-demand)
- MJPEG streaming: ~200KB buffer per client
- Recommend: Limit to 4 concurrent streams

### Browser Compatibility
- ✅ Chrome/Edge (best performance)
- ✅ Firefox (good)
- ✅ Safari (iOS support)
- ⚠️ IE11 (not recommended)

---

## Comparison to External NVR

| Feature | Sentinel Live View | Blue Iris | Frigate | VLC |
|---------|-------------------|-----------|---------|-----|
| Cost | **Free** | $60 | Free | Free |
| Platform | **Web Browser** | Windows Only | Docker | Desktop |
| Setup | **Built-in** | Install | Docker Compose | Manual |
| Grid View | ✅ 2-4×4 | ✅ Unlimited | ✅ Custom | ❌ |
| Click to Full | ✅ | ✅ | ✅ | ❌ |
| Recording | 🔜 Planned | ✅ Pro | ✅ 24/7 | ✅ Manual |
| AI Detection | ❌ | 💰 Plugins | ✅ Built-in | ❌ |
| Mobile App | ✅ Browser | ✅ iOS/Android | ✅ PWA | ❌ |

---

## Alternative: Embed Thingino Web UI

Simplest option - just iframe Thingino's built-in viewer:

```html
<iframe src="http://192.168.1.101/" width="100%" height="600px"></iframe>
```

**Pros:** Zero development, uses Thingino's native interface  
**Cons:** Less integration, can't combine multiple cameras

---

## Recommended Approach

### Phase 1 (Minimal Viable Product)
1. Create `cameras_live.html` with auto-refresh snapshots
2. Add 2×2 grid layout
3. Implement click-to-fullscreen
4. Total effort: **4-6 hours**

### Phase 2 (Enhanced)
1. Add MJPEG streaming proxy
2. Implement 3×3 and 4×4 grids
3. Add snapshot download
4. Total effort: **1 day**

### Phase 3 (Advanced)
1. Recording to SD card
2. PTZ controls
3. Motion detection overlay
4. Total effort: **2-3 days**

---

## Testing Checklist

- [ ] Grid displays all enabled cameras
- [ ] Auto-refresh works (1 FPS)
- [ ] Click expands to fullscreen
- [ ] Fullscreen has higher refresh rate
- [ ] ESC key closes fullscreen
- [ ] Works on mobile browsers
- [ ] Handles offline cameras gracefully
- [ ] Multiple users can view simultaneously
- [ ] Memory usage stays under 500KB

---

## User Documentation

Add to Sentinel README:

```markdown
### Live Camera View

Navigate to `https://sentinel.local/cameras_live.html` to view all cameras in real-time.

**Controls:**
- Click grid size buttons (2×2, 3×3, 4×4) to change layout
- Click any camera tile to view fullscreen
- Press ESC to exit fullscreen
- Press F in fullscreen for native browser fullscreen
- Click 📷 Snapshot to download current frame

**Requirements:**
- Modern web browser (Chrome, Firefox, Safari)
- Cameras must be added in Camera Config first
- Network bandwidth: ~100KB/s per camera
```

---

## Conclusion

✅ **Feasible:** Yes, can be implemented with existing Sentinel infrastructure  
✅ **Useful:** Eliminates need for external NVR for basic monitoring  
✅ **Priority:** High - improves user experience significantly  
✅ **Effort:** 4-8 hours for basic version, 1-2 days for full-featured

**Recommendation:** Start with Phase 1 (auto-refresh grid) as it requires minimal changes and provides immediate value. This addresses your concern about Blue Iris being Windows-only by providing a cross-platform web-based alternative built directly into Sentinel.
