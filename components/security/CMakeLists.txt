idf_component_register(
    SRCS "password_hash.c" "session_mgr.c" "audit_log.c"
    INCLUDE_DIRS "."
    REQUIRES mbedtls nvs_flash spiffs
)
