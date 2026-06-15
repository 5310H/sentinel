idf_component_register(
    SRCS "camera_mgr.c" "camera_zones.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_client cjson storage engine
)
