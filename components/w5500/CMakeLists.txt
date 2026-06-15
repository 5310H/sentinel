idf_component_register(
    SRCS "src/esp_eth_mac_w5500.c" 
         "src/esp_eth_phy_w5500.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_eth esp_timer
    PRIV_REQUIRES esp_driver_gpio
)