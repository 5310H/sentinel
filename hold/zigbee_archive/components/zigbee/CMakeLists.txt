idf_component_register(
    SRCS "zigbee_protocol.c"
         "zigbee_devices.c"
         "zigbee_mgr.c"
         "zigbee_zones.c"
         "zigbee_relays.c"
         "zigbee_groups.c"
         "zigbee_scenes.c"
         "zigbee_automations.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_uart esp_driver_gpio driver freertos esp_timer storage scheduler
)
