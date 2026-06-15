idf_component_register(
    SRCS 
        "esphome_api_client.c"
        "esphome_api_protocol.c"
        "esphome_api_server.c"
        "esphome_zones.c"
    INCLUDE_DIRS "."
    REQUIRES 
        lwip
        nvs_flash
        engine
        storage
        comms
)
