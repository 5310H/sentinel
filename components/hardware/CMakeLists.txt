idf_component_register(
    SRCS "hal_esp32.c"
    INCLUDE_DIRS "."
    # Use REQUIRES for things needed in hal.h
    # Use PRIV_REQUIRES for things only used inside hal_esp32.c
    REQUIRES "storage" "esp_netif" 
    PRIV_REQUIRES esp_driver_gpio driver
)
