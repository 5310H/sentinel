idf_component_register(
    SRCS "mongoose.c"
    INCLUDE_DIRS "."
    REQUIRES lwip esp_event esp_timer app_update esp_partition mbedtls
)

# This is the correct way to inject the SMTP flag into the Mongoose library
target_compile_definitions(${COMPONENT_LIB} PUBLIC 
    -DMG_ARCH=MG_ARCH_ESP32
    -DMG_ENABLE_SMTP=1
    -DMG_ENABLE_MBEDTLS=1
    -DMG_IO_SIZE=1024
    -DMG_MAX_HTTP_HEADERS=15
    -DMG_ENABLE_DIRLIST=0
    -DMG_ENABLE_SSI=0

    # --- PSRAM OPTIMIZATIONS for ESP32 ---
    # Force Mongoose's large I/O buffers into external PSRAM to free up internal SRAM
    -DMG_MALLOC=heap_caps_malloc
    -DMG_CALLOC=heap_caps_calloc
    -DMG_REALLOC=heap_caps_realloc
    -DMG_FREE=heap_caps_free
)