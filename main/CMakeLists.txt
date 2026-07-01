idf_component_register(SRCS "main.c" "hal_esp32.c" "rf_driver.c" "server_cert.c"
    INCLUDE_DIRS "."
    EMBED_FILES "version.txt"
    PRIV_REQUIRES nvs_flash storage engine esphome_api camera tuya scheduler esp_eth esp_event esp_netif esp_driver_rmt esp_driver_i2c esp_driver_gpio)

# Matter and Zigbee disabled to save DRAM
# if(NOT "matter_controller" IN_LIST EXCLUDE_COMPONENTS)
#     idf_component_optional_requires(PRIVATE matter_controller)
# endif()