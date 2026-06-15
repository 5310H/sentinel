# Sentinel Mock - Windows Port Guide

**Target**: Port `sentinel_test` Linux mock to native Windows executable

---

## 1. Dependencies Status

### ✅ Available for Windows

**Mongoose Web Server**
- Status: **Native Windows support**
- Source: Already included in project
- Changes: None needed
- Notes: Uses Winsock2 on Windows automatically

**OpenSSL (TLS/HTTPS)**
- Status: **Pre-built binaries available**
- Source: https://slproweb.com/products/Win32OpenSSL.html
- Install: Win64 OpenSSL v3.x (MSI installer)
- Location: `C:\Program Files\OpenSSL-Win64\`
- Libs: `libssl.lib`, `libcrypto.lib`

**libcurl (HTTP client)**
- Status: **Pre-built binaries available**
- Source: https://curl.se/windows/
- Install: curl-x.x.x-win64-mingw.zip
- Location: Extract to `C:\curl\`
- Libs: `libcurl.dll`, `libcurl.lib`

**cJSON (JSON parser)**
- Status: **Cross-platform, no changes**
- Source: Already included in project
- Changes: None needed
- Notes: Pure C, works on Windows

**pthreads (Threading)**
- Status: **MinGW provides, or use Windows threads**
- Option 1: MinGW-w64 (includes pthread emulation)
- Option 2: Native Windows threads (CreateThread)
- Option 3: C11 threads.h (C11 standard)

### ⚠️ Requires Replacement

**Serial Port (USB Zigbee coordinator)**
- Linux: `termios.h`, `/dev/ttyUSB0`
- Windows: Need COM port API
- Solutions:
  1. **libserialport** (recommended - cross-platform)
  2. Windows serial API directly (CreateFile, SetCommState)
  3. Qt SerialPort module

**POSIX Headers**
- `unistd.h` → Windows equivalents
- `sleep()` → `Sleep()` (note capital S)
- `usleep()` → `Sleep(milliseconds)`
- File paths: `/` → `\\`

---

## 2. Code Changes Needed

### A. Serial Port (zigbee_mock.c, main_mock.c)

**Current Code** (Lines 20-22, zigbee_mock.c):
```c
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
```

**Windows Replacement - Option 1 (libserialport - RECOMMENDED)**:
```c
#ifdef _WIN32
    #include <libserialport.h>
    #define COM_PORT "COM3"
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <termios.h>
    #define COM_PORT "/dev/ttyUSB0"
#endif
```

**Windows Replacement - Option 2 (Native API)**:
```c
#ifdef _WIN32
    #include <windows.h>
    HANDLE hComm;
    
    // Open COM port
    hComm = CreateFile("COM3", 
                       GENERIC_READ | GENERIC_WRITE,
                       0, NULL, OPEN_EXISTING, 0, NULL);
    
    // Configure serial port
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    GetCommState(hComm, &dcbSerialParams);
    dcbSerialParams.BaudRate = CBR_115200;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    SetCommState(hComm, &dcbSerialParams);
    
    // Read/Write
    DWORD bytesRead;
    ReadFile(hComm, buffer, size, &bytesRead, NULL);
    WriteFile(hComm, data, len, &bytesWritten, NULL);
#else
    // Existing Linux termios code
#endif
```

### B. Threading (main_mock.c)

**Current Code** (pthread usage):
```c
pthread_t thread_id;
pthread_create(&thread_id, NULL, thread_func, arg);
```

**Windows Replacement** (if not using MinGW):
```c
#ifdef _WIN32
    #include <windows.h>
    HANDLE hThread;
    hThread = CreateThread(NULL, 0, thread_func, arg, 0, NULL);
#else
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, thread_func, arg);
#endif
```

**OR use C11 threads (cross-platform)**:
```c
#include <threads.h>
thrd_t thread_id;
thrd_create(&thread_id, thread_func, arg);
```

### C. Sleep Functions

**Current Code**:
```c
sleep(1);       // seconds
usleep(1000);   // microseconds
```

**Windows Replacement**:
```c
#ifdef _WIN32
    #include <windows.h>
    #define sleep(x) Sleep((x) * 1000)  // Sleep takes milliseconds
    #define usleep(x) Sleep((x) / 1000)
#else
    #include <unistd.h>
#endif
```

### D. File Paths (storage_mgr.c)

**Current Code**:
```c
#define BASE_PATH "/spiffs"
char path[128];
snprintf(path, 128, "%s/%s", BASE_PATH, filename);
```

**Windows Replacement**:
```c
#ifdef _WIN32
    #define BASE_PATH "C:\\ProgramData\\Sentinel"
    #define PATH_SEP "\\"
#else
    #define BASE_PATH "/spiffs"
    #define PATH_SEP "/"
#endif

char path[128];
snprintf(path, 128, "%s%s%s", BASE_PATH, PATH_SEP, filename);
```

**Or use portable approach**:
```c
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileName(NULL, path, MAX_PATH);  // Get exe directory
    PathRemoveFileSpec(path);  // Remove exe name
    strcat(path, "\\data\\");
#else
    const char *path = "./data/";
#endif
```

### E. Network Socket Initialization

**Add to main() for Windows**:
```c
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    // ... program runs ...
    WSACleanup();
#endif
```

### F. Compiler Directives

**Add to all affected files**:
```c
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "advapi32.lib")
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
#endif
```

---

## 3. Build System Options

### Option A: MinGW-w64 (Easiest - 1-2 hours)

**Why Choose**:
- Drop-in GCC replacement for Windows
- Includes pthread support (POSIX threads)
- Can reuse most of existing Makefile
- Produces native .exe (no Cygwin DLL needed)
- Free and open source

**Installation**:
```bash
# Download: https://www.mingw-w64.org/downloads/
# Or use MSYS2 (recommended):
# https://www.msys2.org/

# Install via MSYS2:
pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-openssl
pacman -S mingw-w64-x86_64-curl
pacman -S mingw-w64-x86_64-cjson
pacman -S mingw-w64-x86_64-libserialport  # For USB serial
```

**Modified Makefile.mingw**:
```makefile
# Use MinGW compiler
CC = x86_64-w64-mingw32-gcc
CFLAGS = -g -O0 -Wall -D_WIN32 -DWIN32_LEAN_AND_MEAN

# Windows libraries
LIBS = -lws2_32 -lssl -lcrypto -lcurl -lcjson -lpthread -lm -lserialport

# Link with Windows subsystem
LDFLAGS = -mconsole  # or -mwindows for GUI

# Output
TARGET = sentinel_test.exe

# Rest similar to existing Makefile
```

**Build**:
```bash
# In MSYS2 MinGW64 shell
make -f Makefile.mingw
```

### Option B: MSVC (Native Windows - 1-2 days)

**Why Choose**:
- Official Microsoft compiler
- Best Windows integration
- Superior debugging (Visual Studio)
- Native Windows threading
- Professional appearance

**Installation**:
- Visual Studio 2022 Community (free)
- Or Build Tools for Visual Studio

**Project Structure**:
```
sentinel_test/
├── sentinel_test.sln          (Visual Studio solution)
├── sentinel_test.vcxproj      (Project file)
├── src/                       (Source files)
├── include/                   (Headers)
├── lib/                       (Pre-built libraries)
│   ├── openssl/
│   ├── curl/
│   └── cjson/
└── bin/                       (Output .exe)
```

**vcxproj Configuration**:
```xml
<PropertyGroup>
  <ConfigurationType>Application</ConfigurationType>
  <PlatformToolset>v143</PlatformToolset>
  <CharacterSet>Unicode</CharacterSet>
</PropertyGroup>

<ItemDefinitionGroup>
  <ClCompile>
    <PreprocessorDefinitions>WIN32;_CONSOLE;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    <AdditionalIncludeDirectories>include;lib/openssl/include;lib/curl/include</AdditionalIncludeDirectories>
  </ClCompile>
  <Link>
    <AdditionalDependencies>ws2_32.lib;libssl.lib;libcrypto.lib;libcurl.lib;cjson.lib;%(AdditionalDependencies)</AdditionalDependencies>
    <AdditionalLibraryDirectories>lib/openssl;lib/curl;lib/cjson</AdditionalLibraryDirectories>
  </Link>
</ItemDefinitionGroup>
```

**Build**:
```bash
# Command line
msbuild sentinel_test.sln /p:Configuration=Release

# Or use Visual Studio GUI
```

### Option C: CMake (Cross-Platform - Best Long-term)

**Why Choose**:
- Single build system for Linux/Windows/macOS
- Auto-detects platform and tools
- Generates Makefiles, VS projects, Xcode projects
- Industry standard
- Future-proof

**CMakeLists.txt** (create in test_linux/):
```cmake
cmake_minimum_required(VERSION 3.15)
project(sentinel_test C)

set(CMAKE_C_STANDARD 11)

# Detect platform
if(WIN32)
    add_definitions(-D_WIN32 -DWIN32_LEAN_AND_MEAN)
    set(PLATFORM_LIBS ws2_32 advapi32)
else()
    set(PLATFORM_LIBS pthread m)
endif()

# Find packages
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# Source files
set(SOURCES
    main_mock.c
    zigbee_mock.c
    tuya_mock.c
    camera_mock.c
    esphome_mock.c
    matter_mock.c
    ../components/storage/storage_mgr.c
    ../components/engine/dispatcher.c
    ../components/engine/otp.c
    ../components/mongoose/mongoose.c
    # ... add all sources
)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/storage
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/mongoose
    ${OPENSSL_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS}
)

# Executable
add_executable(sentinel_test ${SOURCES})

# Link libraries
target_link_libraries(sentinel_test
    ${OPENSSL_LIBRARIES}
    ${CURL_LIBRARIES}
    cjson
    ${PLATFORM_LIBS}
)

# Windows-specific
if(WIN32)
    target_link_libraries(sentinel_test serialport)
endif()

# Install
install(TARGETS sentinel_test DESTINATION bin)
install(DIRECTORY data/ DESTINATION share/sentinel/data)
```

**Build**:
```bash
# Linux
mkdir build && cd build
cmake ..
make

# Windows (MinGW)
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
mingw32-make

# Windows (Visual Studio)
mkdir build && cd build
cmake -G "Visual Studio 17 2022" ..
cmake --build . --config Release

# macOS
mkdir build && cd build
cmake ..
make
```

---

## 4. Estimated Changes

### Minimal Port (MinGW-w64) - 1-2 Hours

**Files to Modify**: 4 files
1. `zigbee_mock.c` - Serial port abstraction (~20 lines)
2. `main_mock.c` - Sleep/unistd replacements (~10 lines)
3. `Makefile.mingw` - New file (~40 lines, copy from Makefile)
4. `storage_mgr.c` - Path separators (~5 lines)

**Total Code Changes**: ~75 lines
**New Code**: 0 lines (just wrappers)
**Build Time**: 5 minutes
**Testing**: 30 minutes

**Deliverable**:
- `sentinel_test.exe` (Windows executable)
- Works with USB Zigbee coordinators on COM ports
- Identical functionality to Linux version

### Full Native Port (MSVC) - 1-2 Days

**Day 1 - Core Porting** (6-8 hours)
1. Replace termios with Windows serial API (~2 hours)
   - 100-150 lines of serial code
2. Replace pthread with Windows threads (~1 hour)
   - 30-50 lines of threading code
3. Windows-specific initialization (~30 minutes)
   - Winsock startup
   - Console setup
4. File path handling (~1 hour)
   - Use Windows directories (AppData, ProgramData)
   - Registry for settings
5. Create Visual Studio project (~1 hour)
   - Solution/project files
   - Configure include paths
   - Link libraries
6. Build and debug (~2-3 hours)
   - Fix compilation errors
   - Resolve linker issues
   - Test basic functionality

**Day 2 - Polish & Features** (6-8 hours)
1. Windows service support (~2 hours)
   - Run as background service
   - Start with Windows
2. System tray icon (~2 hours)
   - Minimize to tray
   - Right-click menu
3. Installer (NSIS) (~2 hours)
   - MSI/EXE installer
   - Registry entries
   - Shortcuts
4. Auto-update (~2 hours)
   - Check for updates
   - Download and install

**Total Code Changes**: ~500-800 lines
**New Code**: ~300-500 lines (Windows-specific features)
**Build Time**: 2-3 minutes
**Testing**: 4-6 hours

**Deliverable**:
- Professional Windows application
- Installer (sentinel_setup.exe)
- Windows service option
- System tray integration
- Auto-update capability

### CMake Cross-Platform - 4-6 Hours

**Hour 1-2 - CMake Setup**
1. Create CMakeLists.txt (~150 lines)
2. Platform detection
3. Find dependencies
4. Configure source files

**Hour 3-4 - Platform Abstractions**
1. Create platform wrapper headers
2. Abstract serial, threading, paths
3. Test on Linux

**Hour 5-6 - Windows Testing**
1. Build on Windows
2. Fix platform-specific issues
3. Verify feature parity

**Total Code Changes**: ~300 lines
**New Code**: ~150 lines (CMake + wrappers)
**Build Time**: 2-5 minutes (first time slower)
**Testing**: 2-3 hours per platform

**Deliverable**:
- Single codebase for Linux/Windows/macOS
- Professional build system
- Easy CI/CD integration
- Future-proof architecture

---

## Summary Table

| Approach | Time | Difficulty | Code Changes | Pros | Cons |
|----------|------|------------|--------------|------|------|
| **MinGW** | 1-2h | Easy | 75 lines | Quick, uses existing code | Less "Windows native" |
| **MSVC** | 1-2d | Medium | 500-800 lines | Native, professional | More work, Windows-only |
| **CMake** | 4-6h | Medium | 300 lines | Cross-platform, future-proof | Learning curve |

## Recommendation

**Phase 1**: Start with **MinGW** to get Windows .exe quickly (1-2 hours)
**Phase 2**: Migrate to **CMake** for maintainability (4-6 hours)
**Phase 3**: Add **Windows-specific polish** (installer, service, tray)

This gives you:
- ✅ Windows support in 2 hours
- ✅ Professional build system in 1 day
- ✅ Native Windows features as needed

---

**Next Steps**: Want me to create the Makefile.mingw or CMakeLists.txt?
