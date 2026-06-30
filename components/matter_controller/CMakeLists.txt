idf_component_register(
    SRCS 
        "matter_controller.c"
        "matter_clusters.c"
        "matter_commissioning.c"
        "matter_zones.c"
        "matter_relays.c"
        "matter_storage.c"
    INCLUDE_DIRS "."
    REQUIRES 
        esp_matter
        esp_matter_controller
        chip
        nvs_flash
        engine
        storage
)
