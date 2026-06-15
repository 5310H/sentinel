idf_component_register(
    SRCS "storage_mgr.c" "cert_mgr.c"
    INCLUDE_DIRS "."
    REQUIRES mongoose security
    PRIV_REQUIRES fatfs sdmmc vfs esp_partition spiffs main nvs_flash
)