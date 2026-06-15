# Instructions for Including server.pem in SPIFFS Partition

## Option 1: Direct SPIFFS Configuration (Recommended)

Add to your main `CMakeLists.txt` or build configuration:

```cmake
# In CMakeLists.txt or idf_build_process call:
set(SPIFFS_IMAGE_PATH ${CMAKE_CURRENT_LIST_DIR}/build/spiffs_image)

# This tells ESP-IDF to use the spiffs_image directory for SPIFFS partition
# Copy server.pem to this directory before building
```

## Option 2: Pre-build Script

Add to your project's CMakeLists.txt:

```cmake
# Before idf_component_register()
add_custom_target(prepare_spiffs
    COMMAND ${CMAKE_CURRENT_LIST_DIR}/../../scripts/prepare_spiffs.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../../
)

# Make this target run before the build
add_dependencies(app prepare_spiffs)
```

## Option 3: Manual Setup (Before Building)

```bash
# From project root:
./scripts/prepare_spiffs.sh
idf.py build
idf.py flash
```

## Verification After Flashing

Once device is running, check logs for:

```
CERT_MGR: Loaded certificate from SPIFFS (3415 bytes)
CERT_MGR: Certificate saved to NVS
```

Or if using embedded:

```
CERT_MGR: Using embedded fallback certificate (3415 bytes)
```

## Partition Table Configuration

Your `partitions.csv` should have a SPIFFS partition defined. Example:

```csv
# Name,   Type, SubType,  Offset,   Size
nvs,      data, nvs,      0x9000,   0x5000
otadata,  data, ota,      0xe000,   0x2000
app0,     app,  ota_0,    0x10000,  0x200000
app1,     app,  ota_1,    0x210000, 0x200000
spiffs,   data, spiffs,   0x410000, 0x1F0000
```

The SPIFFS partition must exist and be large enough to hold the certificate (typically 3-4 KB).
