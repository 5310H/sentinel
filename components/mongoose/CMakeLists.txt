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
)